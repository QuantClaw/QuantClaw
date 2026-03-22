// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/adapter_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <nlohmann/json.hpp>

namespace quantclaw {

ChannelAdapterManager::ChannelAdapterManager(
    int gateway_port, const std::string& auth_token,
    const std::unordered_map<std::string, ChannelConfig>& channels,
    std::shared_ptr<spdlog::logger> logger)
    : gateway_port_(gateway_port),
      auth_token_(auth_token),
      channels_(channels),
      logger_(logger) {}

ChannelAdapterManager::~ChannelAdapterManager() {
  Stop();
}

std::string ChannelAdapterManager::find_adapter_script(
    const std::string& channel_name) const {
  std::string home = platform::home_directory();

  std::vector<std::string> search_paths = {
      home + "/.quantclaw/src/adapters/" + channel_name + ".ts",
  };

  // Relative to the executable's directory
  try {
    auto exe = platform::executable_path();
    auto exe_dir = std::filesystem::path(exe).parent_path();
    search_paths.push_back(
        (exe_dir / "src/adapters" / (channel_name + ".ts")).string());
    search_paths.push_back(
        (exe_dir.parent_path() / "src/adapters" / (channel_name + ".ts"))
            .string());
  } catch (const std::exception&) {}

  for (const auto& path : search_paths) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  return "";
}

bool ChannelAdapterManager::launch_adapter(AdapterProcess& adapter,
                                           const ChannelConfig& config) {
  std::string gateway_url = "ws://127.0.0.1:" + std::to_string(gateway_port_);
  nlohmann::json config_json = config.raw;
  if (!config.token.empty() && !config_json.contains("token")) {
    config_json["token"] = config.token;
  }

  // Build environment variables
  std::vector<std::string> env;
  env.push_back("QUANTCLAW_GATEWAY_URL=" + gateway_url);
  env.push_back("QUANTCLAW_AUTH_TOKEN=" + auth_token_);
  env.push_back("QUANTCLAW_CHANNEL_NAME=" + adapter.name);
  env.push_back("QUANTCLAW_CHANNEL_CONFIG=" + config_json.dump());
  if (!config.token.empty()) {
    std::string env_name = adapter.name;
    for (auto& c : env_name)
      c = static_cast<char>(std::toupper(c));
    env.push_back(env_name + "_BOT_TOKEN=" + config.token);
  }

  // Working directory = adapter script's parent directory
  auto script_dir =
      std::filesystem::path(adapter.script_path).parent_path().string();

  // Try npx tsx first
  std::vector<std::string> args = {"npx", "tsx", adapter.script_path};
  auto pid = platform::spawn_process(args, env, script_dir);

  if (pid == platform::kInvalidPid) {
    // Fallback: try node with .js extension
    std::string js_path = adapter.script_path;
    auto dot = js_path.rfind(".ts");
    if (dot != std::string::npos) {
      js_path.replace(dot, 3, ".js");
    }
    args = {"node", js_path};
    pid = platform::spawn_process(args, env, script_dir);
  }

  if (pid == platform::kInvalidPid) {
    logger_->error("Failed to launch adapter '{}'", adapter.name);
    return false;
  }

  adapter.pid = pid;
  adapter.running = true;
  logger_->info("Launched adapter '{}' (PID: {}, script: {})", adapter.name,
                pid, adapter.script_path);
  return true;
}

void ChannelAdapterManager::kill_adapter(AdapterProcess& adapter) {
  if (!adapter.running || adapter.pid == platform::kInvalidPid)
    return;

  logger_->info("Stopping adapter '{}' (PID: {})", adapter.name, adapter.pid);

  platform::terminate_process(adapter.pid);

  // Wait up to 5 seconds for graceful shutdown
  int exit_code = platform::wait_process(adapter.pid, 5000);
  if (exit_code >= 0) {
    adapter.running = false;
    adapter.pid = platform::kInvalidPid;
    logger_->info("Adapter '{}' stopped", adapter.name);
    return;
  }

  // Force kill
  platform::kill_process(adapter.pid);
  platform::wait_process(adapter.pid, 2000);
  adapter.running = false;
  adapter.pid = platform::kInvalidPid;
  logger_->warn("Adapter '{}' force-killed", adapter.name);
}

void ChannelAdapterManager::Start() {
  if (running_)
    return;

  for (const auto& [name, config] : channels_) {
    if (!config.enabled) {
      logger_->debug("Channel '{}' disabled, skipping", name);
      continue;
    }

    std::string script = find_adapter_script(name);
    if (script.empty()) {
      logger_->warn("No adapter script found for channel '{}', skipping", name);
      continue;
    }

    AdapterProcess proc;
    proc.name = name;
    proc.script_path = script;

    if (launch_adapter(proc, config)) {
      adapters_.push_back(std::move(proc));
    }
  }

  if (!adapters_.empty()) {
    running_ = true;
    monitor_thread_ = std::make_unique<std::thread>(
        &ChannelAdapterManager::monitor_loop, this);
    logger_->info("ChannelAdapterManager started with {} adapter(s)",
                  adapters_.size());
  } else {
    logger_->info("No channel adapters to start");
  }
}

void ChannelAdapterManager::Stop() {
  running_ = false;

  for (auto& adapter : adapters_) {
    kill_adapter(adapter);
  }
  adapters_.clear();

  if (monitor_thread_ && monitor_thread_->joinable()) {
    monitor_thread_->join();
  }
  monitor_thread_.reset();
}

std::vector<std::string> ChannelAdapterManager::RunningAdapters() const {
  std::vector<std::string> names;
  for (const auto& adapter : adapters_) {
    if (adapter.running) {
      names.push_back(adapter.name);
    }
  }
  return names;
}

void ChannelAdapterManager::monitor_loop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    if (!running_)
      break;

    for (auto& adapter : adapters_) {
      if (!adapter.running)
        continue;

      if (!platform::is_process_alive(adapter.pid)) {
        logger_->warn("Adapter '{}' exited unexpectedly", adapter.name);
        adapter.running = false;
        adapter.pid = platform::kInvalidPid;

        // Auto-restart if manager is still running
        if (running_) {
          logger_->info("Restarting adapter '{}'...", adapter.name);
          auto it = channels_.find(adapter.name);
          if (it != channels_.end()) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            launch_adapter(adapter, it->second);
          }
        }
      }
    }
  }
}

}  // namespace quantclaw
