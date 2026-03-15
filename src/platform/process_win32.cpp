// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "quantclaw/platform/process.hpp"

#include <psapi.h>
#include <windows.h>

namespace quantclaw::platform {

ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env,
                        const std::string& working_dir) {
  if (args.empty())
    return kInvalidPid;

  // Build command line
  std::ostringstream cmdline;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      cmdline << " ";
    // Quote args that contain spaces
    if (args[i].find(' ') != std::string::npos) {
      cmdline << "\"" << args[i] << "\"";
    } else {
      cmdline << args[i];
    }
  }
  std::string cmd_str = cmdline.str();

  // Build environment block if env vars specified
  std::string env_block;
  if (!env.empty()) {
    // Get current environment
    char* current_env = GetEnvironmentStringsA();
    if (current_env) {
      const char* p = current_env;
      while (*p) {
        std::string entry(p);
        env_block += entry;
        env_block += '\0';
        p += entry.size() + 1;
      }
      FreeEnvironmentStringsA(current_env);
    }
    for (const auto& e : env) {
      env_block += e;
      env_block += '\0';
    }
    env_block += '\0';
  }

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  BOOL ok = CreateProcessA(
      nullptr, const_cast<char*>(cmd_str.c_str()), nullptr, nullptr, FALSE,
      env.empty() ? 0 : 0,
      env.empty() ? nullptr : const_cast<char*>(env_block.c_str()),
      working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);

  if (!ok)
    return kInvalidPid;

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return pi.dwProcessId;
}

bool is_process_alive(ProcessId pid) {
  if (pid == 0)
    return false;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return false;
  DWORD exit_code;
  BOOL ok = GetExitCodeProcess(h, &exit_code);
  CloseHandle(h);
  return ok && exit_code == STILL_ACTIVE;
}

void terminate_process(ProcessId pid) {
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (h) {
    TerminateProcess(h, 1);
    CloseHandle(h);
  }
}

void kill_process(ProcessId pid) {
  terminate_process(pid);  // Windows has no SIGKILL equivalent
}

void reload_process(ProcessId /*pid*/) {
  // No-op on Windows (no SIGHUP equivalent)
}

int wait_process(ProcessId pid, int timeout_ms) {
  HANDLE h =
      OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return -1;

  DWORD wait_time = (timeout_ms < 0)    ? INFINITE
                    : (timeout_ms == 0) ? 0
                                        : static_cast<DWORD>(timeout_ms);

  DWORD wait_result = WaitForSingleObject(h, wait_time);
  if (wait_result != WAIT_OBJECT_0) {
    CloseHandle(h);
    return -1;
  }

  DWORD exit_code;
  GetExitCodeProcess(h, &exit_code);
  CloseHandle(h);
  return static_cast<int>(exit_code);
}

ExecResult exec_capture(const std::string& command, int timeout_seconds) {
  ExecResult result;

  // Use _popen on Windows
  FILE* pipe = _popen(command.c_str(), "r");
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
        _pclose(pipe);
        result.exit_code = -2;
        return result;
      }
    }
  }

  result.exit_code = _pclose(pipe);
  return result;
}

std::string executable_path() {
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    return std::string(buf, len);
  }
  return "quantclaw.exe";
}

std::string home_directory() {
  const char* userprofile = std::getenv("USERPROFILE");
  if (userprofile)
    return userprofile;
  const char* homedrive = std::getenv("HOMEDRIVE");
  const char* homepath = std::getenv("HOMEPATH");
  if (homedrive && homepath)
    return std::string(homedrive) + homepath;
  return "C:\\";
}

}  // namespace quantclaw::platform

#endif  // _WIN32
