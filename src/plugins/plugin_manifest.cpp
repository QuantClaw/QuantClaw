// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/plugins/plugin_manifest.hpp"

#include <fstream>
#include <stdexcept>

namespace quantclaw {

PluginManifest PluginManifest::Parse(const nlohmann::json& j) {
  PluginManifest m;

  if (!j.contains("id") || !j["id"].is_string()) {
    throw std::runtime_error("Plugin manifest missing required field: id");
  }
  m.id = j["id"].get<std::string>();

  m.name = j.value("name", m.id);
  m.description = j.value("description", "");
  m.version = j.value("version", "");
  m.kind = j.value("kind", "");

  if (j.contains("channels") && j["channels"].is_array()) {
    for (const auto& ch : j["channels"]) {
      if (ch.is_string())
        m.channels.push_back(ch.get<std::string>());
    }
  }
  if (j.contains("providers") && j["providers"].is_array()) {
    for (const auto& p : j["providers"]) {
      if (p.is_string())
        m.providers.push_back(p.get<std::string>());
    }
  }
  if (j.contains("skills") && j["skills"].is_array()) {
    for (const auto& s : j["skills"]) {
      if (s.is_string())
        m.skills.push_back(s.get<std::string>());
    }
  }

  if (j.contains("configSchema")) {
    m.config_schema = j["configSchema"];
  } else {
    m.config_schema = nlohmann::json::object();
  }

  if (j.contains("uiHints") && j["uiHints"].is_object()) {
    for (auto it = j["uiHints"].begin(); it != j["uiHints"].end(); ++it) {
      PluginConfigUiHint hint;
      const auto& v = it.value();
      hint.label = v.value("label", "");
      hint.help = v.value("help", "");
      hint.advanced = v.value("advanced", false);
      hint.sensitive = v.value("sensitive", false);
      hint.placeholder = v.value("placeholder", "");
      if (v.contains("tags") && v["tags"].is_array()) {
        for (const auto& t : v["tags"]) {
          if (t.is_string())
            hint.tags.push_back(t.get<std::string>());
        }
      }
      m.ui_hints[it.key()] = std::move(hint);
    }
  }

  return m;
}

PluginManifest PluginManifest::LoadFromFile(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    throw std::runtime_error("Cannot open plugin manifest: " + path.string());
  }
  nlohmann::json j;
  try {
    ifs >> j;
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error("Invalid JSON in " + path.string() + ": " +
                             e.what());
  }
  return Parse(j);
}

nlohmann::json PluginManifest::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  if (!name.empty() && name != id)
    j["name"] = name;
  if (!description.empty())
    j["description"] = description;
  if (!version.empty())
    j["version"] = version;
  if (!kind.empty())
    j["kind"] = kind;
  if (!channels.empty())
    j["channels"] = channels;
  if (!providers.empty())
    j["providers"] = providers;
  if (!skills.empty())
    j["skills"] = skills;
  if (!config_schema.is_null() && !config_schema.empty()) {
    j["configSchema"] = config_schema;
  }
  return j;
}

std::string plugin_origin_to_string(PluginOrigin origin) {
  switch (origin) {
    case PluginOrigin::kBundled:
      return "bundled";
    case PluginOrigin::kGlobal:
      return "global";
    case PluginOrigin::kWorkspace:
      return "workspace";
    case PluginOrigin::kConfig:
      return "config";
  }
  return "unknown";
}

}  // namespace quantclaw
