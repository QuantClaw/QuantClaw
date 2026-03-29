// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "quantclaw/constants.hpp"

namespace quantclaw::platform {

#ifdef __APPLE__
namespace detail {
std::string shell_quote(std::string_view value);
std::string xml_escape(std::string_view value);
}  // namespace detail
#endif

// Cross-platform daemon/service management.
// Linux: systemd user service.
// macOS: launchd user agent.
// Windows: background process with PID file (no Windows Service for now).
class ServiceManager {
 public:
  explicit ServiceManager(std::shared_ptr<spdlog::logger> logger);
  ~ServiceManager() = default;

  // Install the service definition for the current platform.
  int install(int port = kLegacyGatewayPort);

  // Uninstall the service.
  int uninstall();

  // Start the service.
  int start();

  // Stop the service.
  int stop();

  // Restart the service.
  int restart();

  // Print service status.
  int status();

  // Check if the service is currently running.
  bool is_running() const;

  // Get the PID of the running service.
  int get_pid() const;

  // Write PID file.
  void write_pid(int pid);

  // Remove PID file.
  void remove_pid();

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::string state_dir_;
  std::string pid_file_;
  std::string log_file_;

  std::string service_path() const;
};

}  // namespace quantclaw::platform
