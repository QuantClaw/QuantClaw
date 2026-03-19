// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace quantclaw::platform {

// Platform-neutral process ID type
#ifdef _WIN32
using ProcessId = unsigned long;  // DWORD
#else
using ProcessId = int;  // pid_t
#endif

constexpr ProcessId kInvalidPid = 0;

// Result of exec_capture()
struct ExecResult {
  std::string output;
  int exit_code = -1;
};

// Spawn a child process. Returns PID (>0) on success, 0 on failure.
// `args[0]` is the executable path.
// `env` is a list of "KEY=VALUE" strings appended to the child environment.
ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env = {},
                        const std::string& working_dir = "");

// Check if a process is alive.
bool is_process_alive(ProcessId pid);

// Send a graceful stop signal (SIGTERM on Unix, TerminateProcess on Windows).
void terminate_process(ProcessId pid);

// Force-kill a process (SIGKILL on Unix, TerminateProcess on Windows).
void kill_process(ProcessId pid);

// Send a reload signal (SIGHUP on Unix, no-op on Windows).
void reload_process(ProcessId pid);

// Wait for process to exit. Returns exit code, or -1 on error.
// If `timeout_ms` > 0, waits up to that many milliseconds (0 = non-blocking).
// If `timeout_ms` < 0, waits indefinitely.
int wait_process(ProcessId pid, int timeout_ms = -1);

// Execute a command and capture stdout. Blocks until complete.
// `timeout_seconds` <= 0 means no timeout.
// `working_dir` optionally sets the child's working directory.
ExecResult exec_capture(const std::string& command, int timeout_seconds = 30,
                        const std::string& working_dir = "");

// Get the absolute path of the current executable.
std::string executable_path();

// Get the user's home directory.
std::string home_directory();

}  // namespace quantclaw::platform
