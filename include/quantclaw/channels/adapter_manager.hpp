// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/platform/process.hpp"

namespace quantclaw {

// Manages channel adapter subprocesses. Each enabled channel in the config
// gets an adapter script launched with environment variables:
//   QUANTCLAW_GATEWAY_URL    — WebSocket URL to the gateway
//   QUANTCLAW_AUTH_TOKEN     — authentication token
//   QUANTCLAW_CHANNEL_NAME   — channel name (e.g. "discord")
//   QUANTCLAW_CHANNEL_CONFIG — JSON string of the channel's config
//
// Adapters are located in:
//   1. ~/.quantclaw/adapters/<name>_bot.js    (user-installed)
//   2. <install_prefix>/adapters/<name>_bot.js (bundled with QuantClaw)
class ChannelAdapterManager {
 public:
  ChannelAdapterManager(
      int gateway_port, const std::string& auth_token,
      const std::unordered_map<std::string, ChannelConfig>& channels,
      std::shared_ptr<spdlog::logger> logger);

  ~ChannelAdapterManager();

  // Start all enabled channel adapters
  void Start();

  // Stop all running adapters
  void Stop();

  // Get list of running adapter names
  std::vector<std::string> RunningAdapters() const;

 private:
  struct AdapterProcess {
    std::string name;
    std::string script_path;
    platform::ProcessId pid = platform::kInvalidPid;
    bool running = false;
  };

  // Find the adapter script for a channel name
  std::string find_adapter_script(const std::string& channel_name) const;

  // Launch a single adapter subprocess
  bool launch_adapter(AdapterProcess& adapter, const ChannelConfig& config);

  // Kill a single adapter subprocess
  void kill_adapter(AdapterProcess& adapter);

  // Monitor thread: restart crashed adapters
  void monitor_loop();

  int gateway_port_;
  std::string auth_token_;
  std::unordered_map<std::string, ChannelConfig> channels_;
  std::shared_ptr<spdlog::logger> logger_;

  std::vector<AdapterProcess> adapters_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> monitor_thread_;
};

}  // namespace quantclaw
