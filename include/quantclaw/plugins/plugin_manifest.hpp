// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace quantclaw {

struct PluginConfigUiHint {
  std::string label;
  std::string help;
  std::vector<std::string> tags;
  bool advanced = false;
  bool sensitive = false;
  std::string placeholder;
};

struct PluginManifest {
  std::string id;
  std::string name;
  std::string description;
  std::string version;
  std::string kind;  // "memory" or empty
  std::vector<std::string> channels;
  std::vector<std::string> providers;
  std::vector<std::string> skills;
  nlohmann::json config_schema;
  std::map<std::string, PluginConfigUiHint> ui_hints;

  static PluginManifest Parse(const nlohmann::json& j);
  static PluginManifest LoadFromFile(const std::filesystem::path& path);
  nlohmann::json ToJson() const;
};

enum class PluginOrigin {
  kBundled,
  kGlobal,
  kWorkspace,
  kConfig,
};

std::string plugin_origin_to_string(PluginOrigin origin);

struct PluginCandidate {
  std::string id_hint;
  std::filesystem::path source;
  std::filesystem::path root_dir;
  PluginOrigin origin;
  std::string package_name;
  std::string package_version;
  std::string package_description;
  std::optional<PluginManifest> manifest;
};

}  // namespace quantclaw
