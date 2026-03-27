// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.platform.process;

import std;

export namespace quantclaw::platform {

#ifdef _WIN32
using ProcessId = unsigned long;
#else
using ProcessId = int;
#endif

constexpr ProcessId kInvalidPid = 0;

struct ExecResult {
  std::string output;
  int exit_code = -1;
};

ProcessId spawn_process(const std::vector<std::string>& args,
                        const std::vector<std::string>& env = {},
                        const std::string& working_dir = "");

bool is_process_alive(ProcessId pid);
void terminate_process(ProcessId pid);
void kill_process(ProcessId pid);
void reload_process(ProcessId pid);
int wait_process(ProcessId pid, int timeout_ms = -1);

ExecResult exec_capture(const std::string& command, int timeout_seconds = 30,
                        const std::string& working_dir = "");

std::string executable_path();
std::string home_directory();

}  // namespace quantclaw::platform