// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

#include <poll.h>
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

#include "quantclaw/platform/process.hpp"

namespace quantclaw::platform {

ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env,
                        const std::string& working_dir) {
  if (args.empty())
    return kInvalidPid;

  pid_t pid = fork();
  if (pid < 0)
    return kInvalidPid;

  if (pid == 0) {
    // Child process
    if (!working_dir.empty()) {
      if (chdir(working_dir.c_str()) != 0)
        _exit(1);
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
  if (pid <= 0)
    return false;
  return kill(pid, 0) == 0;
}

void terminate_process(ProcessId pid) {
  if (pid > 0)
    kill(pid, SIGTERM);
}

void kill_process(ProcessId pid) {
  if (pid > 0)
    kill(pid, SIGKILL);
}

void reload_process(ProcessId pid) {
  if (pid > 0)
    kill(pid, SIGHUP);
}

int wait_process(ProcessId pid, int timeout_ms) {
  if (pid <= 0)
    return -1;

  if (timeout_ms == 0) {
    // Non-blocking
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r <= 0)
      return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }

  if (timeout_ms < 0) {
    // Wait forever
    int status;
    pid_t r = waitpid(pid, &status, 0);
    if (r <= 0)
      return -1;
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

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
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
      if (chdir(working_dir.c_str()) != 0)
        _exit(1);
    }

    // Apply resource limits in the child (not the host process).
    // Only on Linux — macOS has different rlimit semantics for some resources.
#ifdef __linux__
    struct rlimit cpu_lim = {30, 60};
    setrlimit(RLIMIT_CPU, &cpu_lim);
    struct rlimit mem_lim = {256ULL * 1024 * 1024, 512ULL * 1024 * 1024};
    setrlimit(RLIMIT_AS, &mem_lim);
    struct rlimit fsz_lim = {64ULL * 1024 * 1024, 128ULL * 1024 * 1024};
    setrlimit(RLIMIT_FSIZE, &fsz_lim);
    struct rlimit nproc_lim = {32, 64};
    setrlimit(RLIMIT_NPROC, &nproc_lim);
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
      if (errno == EINTR)
        continue;
      break;  // error
    }
    if (pr == 0) {
      timed_out = true;
      break;  // poll timed out
    }

    ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
    if (n <= 0)
      break;  // EOF or error
    buffer[n] = '\0';
    result.output += buffer;
  }

  close(pipefd[0]);

  if (timed_out) {
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    result.exit_code = -2;
    return result;
  }

  int status = 0;
  waitpid(pid, &status, 0);
  result.exit_code =
      WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
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
