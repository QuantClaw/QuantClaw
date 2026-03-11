// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/plugins/plugin_manifest.hpp"

namespace quantclaw {

enum class PluginStatus {
  kLoaded,
  kDisabled,
  kError,
};

std::string plugin_status_to_string(PluginStatus s);

struct PluginRecord {
  std::string id;
  std::string name;
  std::string version;
  std::string description;
  std::string kind;
  std::filesystem::path source;
  std::filesystem::path root_dir;
  PluginOrigin origin;
  bool enabled = true;
  PluginStatus status = PluginStatus::kLoaded;
  std::string error;

  // Declared capabilities from manifest + sidecar
  std::vector<std::string> tool_names;
  std::vector<std::string> channel_ids;
  std::vector<std::string> provider_ids;
  std::vector<std::string> service_ids;
  std::vector<std::string> skill_names;
  std::vector<std::string> gateway_methods;
  std::vector<std::string> cli_commands;
  std::vector<std::string> command_names;
  std::vector<std::string> hook_names;
  int http_handler_count = 0;

  nlohmann::json config_schema;
  nlohmann::json plugin_config;  // from config.plugins.entries[id].config
};

class PluginRegistry {
 public:
  explicit PluginRegistry(std::shared_ptr<spdlog::logger> logger);

  // Discover and load all plugins
  void Discover(const QuantClawConfig& config,
                const std::filesystem::path& workspace_dir);

  // Get all plugin records
  const std::vector<PluginRecord>& Plugins() const {
    return plugins_;
  }

  // Find a plugin by ID
  const PluginRecord* Find(const std::string& id) const;

  // Get enabled plugin IDs
  std::vector<std::string> EnabledPluginIds() const;

  // Check if a plugin is enabled
  bool IsEnabled(const std::string& id) const;

  // Update plugin records with capabilities reported by sidecar
  void UpdateFromSidecar(const nlohmann::json& sidecar_plugin_list);

  // JSON summary of all plugins
  nlohmann::json ToJson() const;

 private:
  // Discovery helpers
  std::vector<PluginCandidate>
  discover_candidates(const QuantClawConfig& config,
                      const std::filesystem::path& workspace_dir);

  void scan_directory(const std::filesystem::path& dir, PluginOrigin origin,
                      std::vector<PluginCandidate>& out);

  bool should_enable(const std::string& plugin_id, PluginOrigin origin,
                     const QuantClawConfig& config) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::vector<PluginRecord> plugins_;
  std::map<std::string, size_t> id_index_;
};

}  // namespace quantclaw
