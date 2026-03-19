// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "quantclaw/common/defer.hpp"
#include "quantclaw/platform/process.hpp"

#include <poll.h>

namespace quantclaw::platform {

ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env,
                        const std::string& working_dir) {
  if (args.empty()) {
    return kInvalidPid;
  }

  pid_t pid = fork();
  if (pid < 0) {
    return kInvalidPid;
  }

  if (pid == 0) {
    // Child process
    if (!working_dir.empty()) {
      if (chdir(working_dir.c_str()) != 0) {
        _exit(1);
      }
    }
    for (const auto& e : env) {
      auto eq = e.find('=');
      if (eq != std::string::npos) {
        setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
      }
    }
    std::vector<char*> c_args;
    for (const auto& a : args) {
      c_args.push_back(const_cast<char*>(a.c_str()));
    }
    c_args.push_back(nullptr);
    execvp(c_args[0], c_args.data());
    _exit(127);
  }

  return pid;
}

bool is_process_alive(ProcessId pid) {
  if (pid <= 0) {
    return false;
  }
  return kill(pid, 0) == 0;
}

void terminate_process(ProcessId pid) {
  if (pid > 0) {
    kill(pid, SIGTERM);
  }
}

void kill_process(ProcessId pid) {
  if (pid > 0) {
    kill(pid, SIGKILL);
  }
}

void reload_process(ProcessId pid) {
  if (pid > 0) {
    kill(pid, SIGHUP);
  }
}

int wait_process(ProcessId pid, int timeout_ms) {
  if (pid <= 0) {
    return -1;
  }

  if (timeout_ms == 0) {
    // Non-blocking
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r <= 0) {
      return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }

  if (timeout_ms < 0) {
    // Wait forever
    int status;
    pid_t r = waitpid(pid, &status, 0);
    if (r <= 0) {
      return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }

  // Timed wait via polling
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r > 0) {
      return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return -1;  // Timeout
}

ExecResult exec_capture(const std::string& command, int timeout_seconds,
                        const std::string& working_dir) {
  ExecResult result;

  // Create a pipe for the child's stdout+stderr.
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    result.exit_code = -1;
    return result;
  }
  // Auto-close read end at function exit (all paths).
  DEFER(close(pipefd[0]));

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[1]);  // Close write end; DEFER handles read end.
    result.exit_code = -1;
    return result;
  }

  if (pid == 0) {
    // ---- Child process ----
    close(pipefd[0]);  // close read end
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    // Change working directory if requested.
    if (!working_dir.empty()) {
      if (chdir(working_dir.c_str()) != 0) {
        _exit(1);
      }
    }

    // Apply resource limits in the child (not the host process).
    // Only on Linux — macOS has different rlimit semantics for some resources.
#ifdef __linux__
    struct rlimit cpu_lim = {30, 60};
    if (setrlimit(RLIMIT_CPU, &cpu_lim) != 0) {
      _exit(126);  // Resource limit setup failed
    }
    struct rlimit mem_lim = {256ULL * 1024 * 1024, 512ULL * 1024 * 1024};
    if (setrlimit(RLIMIT_AS, &mem_lim) != 0) {
      _exit(126);
    }
    struct rlimit fsz_lim = {64ULL * 1024 * 1024, 128ULL * 1024 * 1024};
    if (setrlimit(RLIMIT_FSIZE, &fsz_lim) != 0) {
      _exit(126);
    }
    struct rlimit nproc_lim = {32, 64};
    if (setrlimit(RLIMIT_NPROC, &nproc_lim) != 0) {
      _exit(126);
    }
#endif

    execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
    _exit(127);
  }

  // ---- Parent process ----
  close(pipefd[1]);  // close write end

  auto start = std::chrono::steady_clock::now();
  auto deadline = (timeout_seconds > 0)
                      ? start + std::chrono::seconds(timeout_seconds)
                      : std::chrono::steady_clock::time_point::max();

  char buffer[1024];
  bool timed_out = false;
  int child_status = -1;  // Track child exit status; -1 means not yet reaped
  bool child_reaped = false;

  // Use poll() so we never block indefinitely on read.
  struct pollfd pfd;
  pfd.fd = pipefd[0];
  pfd.events = POLLIN;

  for (;;) {
    int remaining_ms = -1;  // infinite if no timeout
    if (timeout_seconds > 0) {
      auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        timed_out = true;
        break;
      }
      remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
              .count());
      if (remaining_ms <= 0) {
        timed_out = true;
        break;
      }
    }

    int pr = poll(&pfd, 1, remaining_ms);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;  // error
    }
    if (pr == 0) {
      timed_out = true;
      break;  // poll timed out
    }

    ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;  // read error
    }
    if (n == 0) {
      // EOF - child closed output, but may still be running.
      // Check if process has exited (non-blocking).
      int status;
      pid_t result_pid = waitpid(pid, &status, WNOHANG);
      if (result_pid == pid) {
        // Process exited - save status and exit loop
        child_status = status;
        child_reaped = true;
        break;
      }
      // Still running; continue loop to enforce timeout.
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    buffer[n] = '\0';
    result.output += buffer;
  }

  // DEFER will close pipefd[0] at function exit.

  if (timed_out) {
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    result.exit_code = -2;
    return result;
  }

  // If we haven't reaped the child yet, do it now
  if (!child_reaped) {
    waitpid(pid, &child_status, 0);
  }

  result.exit_code = WIFEXITED(child_status) ? WEXITSTATUS(child_status)
                                             : 128 + WTERMSIG(child_status);
  return result;
}

std::string executable_path() {
  char buf[4096];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    return std::string(buf);
  }
  return "quantclaw";
}

std::string home_directory() {
  const char* home = std::getenv("HOME");
  return home ? home : "/tmp";
}

}  // namespace quantclaw::platform

#endif  // !_WIN32
