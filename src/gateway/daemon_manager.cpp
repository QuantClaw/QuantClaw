// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/daemon_manager.hpp"

namespace quantclaw::gateway {

DaemonManager::DaemonManager(std::shared_ptr<spdlog::logger> logger)
    : service_(std::move(logger)) {}

int DaemonManager::Install(int port) {
  return service_.install(port);
}
int DaemonManager::Uninstall() {
  return service_.uninstall();
}
int DaemonManager::Start() {
  return service_.start();
}
int DaemonManager::Stop() {
  return service_.stop();
}
int DaemonManager::Restart() {
  return service_.restart();
}
int DaemonManager::Status() {
  return service_.status();
}
bool DaemonManager::IsRunning() const {
  return service_.is_running();
}
int DaemonManager::GetPid() const {
  return service_.get_pid();
}
void DaemonManager::WritePid(int pid) {
  service_.write_pid(pid);
}
void DaemonManager::RemovePid() {
  service_.remove_pid();
}

}  // namespace quantclaw::gateway
