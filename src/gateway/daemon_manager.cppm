// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.gateway.daemon_manager;

import std;
import quantclaw.constants;
import quantclaw.platform.service;

namespace spdlog {
class logger;
}

export namespace quantclaw::gateway {

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