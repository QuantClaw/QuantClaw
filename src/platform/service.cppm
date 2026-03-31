// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.platform.service;

import std;
import quantclaw.constants;

export namespace quantclaw::platform {

class ServiceManager {
 public:
  explicit ServiceManager(std::shared_ptr<spdlog::logger> logger);
  ~ServiceManager() = default;

  int install(int port = kLegacyGatewayPort);
  int uninstall();
  int start();
  int stop();
  int restart();
  int status();
  bool is_running() const;
  int get_pid() const;
  void write_pid(int pid);
  void remove_pid();

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::string state_dir_;
  std::string pid_file_;
  std::string log_file_;

  std::string service_path() const;
};

}  // namespace quantclaw::platform