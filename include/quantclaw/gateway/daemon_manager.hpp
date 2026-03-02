// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include "quantclaw/platform/service.hpp"
#include "quantclaw/constants.hpp"

namespace quantclaw::gateway {

// Manages the QuantClaw gateway as a platform service.
// Linux: systemd user service. Windows: background process with PID file.
// Thin wrapper around platform::ServiceManager.
class DaemonManager {
 public:
  explicit DaemonManager(std::shared_ptr<spdlog::logger> logger);

  int Install(int port = kLegacyGatewayPort);
  int Uninstall();
  int Start();
  int Stop();
  int Restart();
  int Status();

  bool IsRunning() const;
  int GetPid() const;

  void WritePid(int pid);
  void RemovePid();

 private:
  platform::ServiceManager service_;
};

}  // namespace quantclaw::gateway
