// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

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

ExecResult exec_capture(const std::string& command, int timeout_seconds) {
  ExecResult result;
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    result.exit_code = -1;
    return result;
  }

  char buffer[1024];
  auto start = std::chrono::steady_clock::now();
  while (fgets(buffer, sizeof(buffer), pipe)) {
    result.output += buffer;
    if (timeout_seconds > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() > timeout_seconds) {
        pclose(pipe);
        result.exit_code = -2;  // timeout
        return result;
      }
    }
  }

  int status = pclose(pipe);
  result.exit_code = WEXITSTATUS(status);
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
