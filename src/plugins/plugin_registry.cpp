// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/plugins/plugin_registry.hpp"

#include <algorithm>
#include <fstream>

namespace quantclaw {

namespace {

std::filesystem::path get_quantclaw_home() {
  const char* home = std::getenv("HOME");
  if (!home)
    return "/tmp/.quantclaw";
  return std::filesystem::path(home) / ".quantclaw";
}

// Bundled plugins enabled by default (matching OpenClaw)
const std::vector<std::string> kBundledEnabledByDefault = {
    "device-pair",
    "phone-control",
    "talk-voice",
};

}  // namespace

std::string plugin_status_to_string(PluginStatus s) {
  switch (s) {
    case PluginStatus::kLoaded:
      return "loaded";
    case PluginStatus::kDisabled:
      return "disabled";
    case PluginStatus::kError:
      return "error";
  }
  return "unknown";
}

PluginRegistry::PluginRegistry(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void PluginRegistry::Discover(const QuantClawConfig& config,
                              const std::filesystem::path& workspace_dir) {
  plugins_.clear();
  id_index_.clear();

  auto candidates = discover_candidates(config, workspace_dir);

  // Deduplicate: first seen wins (config > workspace > global > bundled)
  std::map<std::string, const PluginCandidate*> seen;
  for (const auto& c : candidates) {
    std::string id = c.manifest ? c.manifest->id : c.id_hint;
    if (seen.find(id) == seen.end()) {
      seen[id] = &c;
    }
  }

  for (const auto& [id, candidate] : seen) {
    PluginRecord record;
    record.id = id;
    record.source = candidate->source;
    record.root_dir = candidate->root_dir;
    record.origin = candidate->origin;

    if (candidate->manifest) {
      const auto& m = *candidate->manifest;
      record.name = m.name;
      record.version = m.version;
      record.description = m.description;
      record.kind = m.kind;
      record.channel_ids = m.channels;
      record.provider_ids = m.providers;
      record.skill_names = m.skills;
      record.config_schema = m.config_schema;
    } else {
      record.name =
          candidate->package_name.empty() ? id : candidate->package_name;
      record.version = candidate->package_version;
      record.description = candidate->package_description;
    }

    record.enabled = should_enable(id, candidate->origin, config);
    record.status =
        record.enabled ? PluginStatus::kLoaded : PluginStatus::kDisabled;

    // Load plugin-specific config
    if (config.plugins_config.contains("entries") &&
        config.plugins_config["entries"].contains(id) &&
        config.plugins_config["entries"][id].contains("config")) {
      record.plugin_config = config.plugins_config["entries"][id]["config"];
    }

    id_index_[id] = plugins_.size();
    plugins_.push_back(std::move(record));
  }

  logger_->info("Plugin registry: {} plugins discovered, {} enabled",
                plugins_.size(), EnabledPluginIds().size());
}

const PluginRecord* PluginRegistry::Find(const std::string& id) const {
  auto it = id_index_.find(id);
  if (it == id_index_.end())
    return nullptr;
  return &plugins_[it->second];
}

std::vector<std::string> PluginRegistry::EnabledPluginIds() const {
  std::vector<std::string> result;
  for (const auto& p : plugins_) {
    if (p.enabled)
      result.push_back(p.id);
  }
  return result;
}

bool PluginRegistry::IsEnabled(const std::string& id) const {
  auto rec = Find(id);
  return rec && rec->enabled;
}

void PluginRegistry::UpdateFromSidecar(
    const nlohmann::json& sidecar_plugin_list) {
  if (!sidecar_plugin_list.is_object() ||
      !sidecar_plugin_list.contains("plugins") ||
      !sidecar_plugin_list["plugins"].is_array()) {
    logger_->warn("Invalid sidecar plugin list format");
    return;
  }

  for (const auto& entry : sidecar_plugin_list["plugins"]) {
    if (!entry.is_object())
      continue;
    std::string id = entry.value("id", "");
    if (id.empty())
      continue;

    auto it = id_index_.find(id);
    if (it == id_index_.end()) {
      // Plugin loaded by sidecar but not in manifest registry — add it
      PluginRecord record;
      record.id = id;
      record.name = entry.value("name", id);
      record.version = entry.value("version", "");
      record.enabled = true;
      record.status = PluginStatus::kLoaded;
      record.origin = PluginOrigin::kConfig;
      id_index_[id] = plugins_.size();
      plugins_.push_back(std::move(record));
      it = id_index_.find(id);
    }

    auto& rec = plugins_[it->second];

    auto get_strings = [](const nlohmann::json& j,
                          const std::string& key) -> std::vector<std::string> {
      std::vector<std::string> result;
      if (j.contains(key) && j[key].is_array()) {
        for (const auto& v : j[key]) {
          if (v.is_string())
            result.push_back(v.get<std::string>());
        }
      }
      return result;
    };

    auto merge_unique = [](std::vector<std::string>& dest,
                           const std::vector<std::string>& src) {
      for (const auto& s : src) {
        if (std::find(dest.begin(), dest.end(), s) == dest.end()) {
          dest.push_back(s);
        }
      }
    };

    merge_unique(rec.tool_names, get_strings(entry, "tools"));
    merge_unique(rec.hook_names, get_strings(entry, "hooks"));
    merge_unique(rec.service_ids, get_strings(entry, "services"));
    merge_unique(rec.provider_ids, get_strings(entry, "providers"));
    merge_unique(rec.command_names, get_strings(entry, "commands"));
    merge_unique(rec.gateway_methods, get_strings(entry, "gatewayMethods"));
    merge_unique(rec.channel_ids, get_strings(entry, "channels"));
    merge_unique(rec.cli_commands, get_strings(entry, "cliEntries"));

    if (entry.contains("httpHandlers") && entry["httpHandlers"].is_number()) {
      rec.http_handler_count = entry["httpHandlers"].get<int>();
    }

    logger_->debug("Updated plugin '{}' capabilities: {} tools, {} hooks", id,
                   rec.tool_names.size(), rec.hook_names.size());
  }
}

nlohmann::json PluginRegistry::ToJson() const {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : plugins_) {
    nlohmann::json j;
    j["id"] = p.id;
    j["name"] = p.name;
    if (!p.version.empty())
      j["version"] = p.version;
    if (!p.description.empty())
      j["description"] = p.description;
    if (!p.kind.empty())
      j["kind"] = p.kind;
    j["source"] = p.source.string();
    j["origin"] = plugin_origin_to_string(p.origin);
    j["enabled"] = p.enabled;
    j["status"] = plugin_status_to_string(p.status);
    if (!p.error.empty())
      j["error"] = p.error;
    if (!p.tool_names.empty())
      j["tools"] = p.tool_names;
    if (!p.channel_ids.empty())
      j["channels"] = p.channel_ids;
    if (!p.provider_ids.empty())
      j["providers"] = p.provider_ids;
    if (!p.service_ids.empty())
      j["services"] = p.service_ids;
    if (!p.skill_names.empty())
      j["skills"] = p.skill_names;
    if (!p.gateway_methods.empty())
      j["gatewayMethods"] = p.gateway_methods;
    if (!p.cli_commands.empty())
      j["cliCommands"] = p.cli_commands;
    if (!p.command_names.empty())
      j["commands"] = p.command_names;
    if (!p.hook_names.empty())
      j["hooks"] = p.hook_names;
    if (p.http_handler_count > 0)
      j["httpHandlers"] = p.http_handler_count;
    arr.push_back(j);
  }
  return arr;
}

std::vector<PluginCandidate> PluginRegistry::discover_candidates(
    const QuantClawConfig& config, const std::filesystem::path& workspace_dir) {
  std::vector<PluginCandidate> candidates;
  auto qc_home = get_quantclaw_home();

  // 1. Config-specified paths (highest priority)
  if (config.plugins_config.contains("load") &&
      config.plugins_config["load"].contains("paths") &&
      config.plugins_config["load"]["paths"].is_array()) {
    for (const auto& path_val : config.plugins_config["load"]["paths"]) {
      if (path_val.is_string()) {
        scan_directory(path_val.get<std::string>(), PluginOrigin::kConfig,
                       candidates);
      }
    }
  }

  // Config installs
  if (config.plugins_config.contains("installs") &&
      config.plugins_config["installs"].is_object()) {
    for (auto it = config.plugins_config["installs"].begin();
         it != config.plugins_config["installs"].end(); ++it) {
      if (it.value().contains("installPath") &&
          it.value()["installPath"].is_string()) {
        auto install_path =
            std::filesystem::path(it.value()["installPath"].get<std::string>());
        if (std::filesystem::exists(install_path)) {
          scan_directory(install_path, PluginOrigin::kConfig, candidates);
        }
      }
    }
  }

  // 2. Workspace plugins
  if (!workspace_dir.empty()) {
    auto ws_plugins = workspace_dir / ".openclaw" / "plugins";
    scan_directory(ws_plugins, PluginOrigin::kWorkspace, candidates);

    auto ws_qc_plugins = workspace_dir / ".quantclaw" / "plugins";
    scan_directory(ws_qc_plugins, PluginOrigin::kWorkspace, candidates);
  }

  // 3. Global plugins
  scan_directory(qc_home / "plugins", PluginOrigin::kGlobal, candidates);
  scan_directory(qc_home / "extensions", PluginOrigin::kGlobal, candidates);

  // 4. Bundled plugins (lowest priority)
  auto bundled_dir = qc_home / "bundled-plugins";
  scan_directory(bundled_dir, PluginOrigin::kBundled, candidates);

  return candidates;
}

void PluginRegistry::scan_directory(const std::filesystem::path& dir,
                                    PluginOrigin origin,
                                    std::vector<PluginCandidate>& out) {
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
    return;
  }

  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_directory())
      continue;

    auto manifest_path = entry.path() / "openclaw.plugin.json";
    if (!std::filesystem::exists(manifest_path)) {
      // Also check for quantclaw.plugin.json
      manifest_path = entry.path() / "quantclaw.plugin.json";
      if (!std::filesystem::exists(manifest_path))
        continue;
    }

    PluginCandidate candidate;
    candidate.root_dir = entry.path();
    candidate.source = manifest_path;
    candidate.origin = origin;
    candidate.id_hint = entry.path().filename().string();

    try {
      candidate.manifest = PluginManifest::LoadFromFile(manifest_path);
      candidate.id_hint = candidate.manifest->id;
    } catch (const std::exception& e) {
      logger_->warn("Failed to parse plugin manifest {}: {}",
                    manifest_path.string(), e.what());
      continue;
    }

    // Try reading package.json for extra metadata
    auto pkg_path = entry.path() / "package.json";
    if (std::filesystem::exists(pkg_path)) {
      try {
        std::ifstream ifs(pkg_path);
        nlohmann::json pkg;
        ifs >> pkg;
        candidate.package_name = pkg.value("name", "");
        candidate.package_version = pkg.value("version", "");
        candidate.package_description = pkg.value("description", "");
      } catch (const std::exception&) {
        // package.json is optional metadata
      }
    }

    out.push_back(std::move(candidate));
  }
}

bool PluginRegistry::should_enable(const std::string& plugin_id,
                                   PluginOrigin origin,
                                   const QuantClawConfig& config) const {
  const auto& pc = config.plugins_config;

  // Global disable
  if (pc.contains("enabled") && pc["enabled"].is_boolean() &&
      !pc["enabled"].get<bool>()) {
    return false;
  }

  // Deny list
  if (pc.contains("deny") && pc["deny"].is_array()) {
    for (const auto& d : pc["deny"]) {
      if (d.is_string() && d.get<std::string>() == plugin_id)
        return false;
    }
  }

  // Allow list (if set, only allow listed plugins)
  if (pc.contains("allow") && pc["allow"].is_array() && !pc["allow"].empty()) {
    bool found = false;
    for (const auto& a : pc["allow"]) {
      if (a.is_string() && a.get<std::string>() == plugin_id) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }

  // Memory slot override
  if (pc.contains("slots") && pc["slots"].contains("memory") &&
      pc["slots"]["memory"].is_string() &&
      pc["slots"]["memory"].get<std::string>() == plugin_id) {
    return true;
  }

  // Per-plugin enabled flag
  if (pc.contains("entries") && pc["entries"].contains(plugin_id)) {
    const auto& entry = pc["entries"][plugin_id];
    if (entry.contains("enabled") && entry["enabled"].is_boolean()) {
      return entry["enabled"].get<bool>();
    }
  }

  // Bundled plugins: only default-enabled ones are on by default
  if (origin == PluginOrigin::kBundled) {
    return std::find(kBundledEnabledByDefault.begin(),
                     kBundledEnabledByDefault.end(),
                     plugin_id) != kBundledEnabledByDefault.end();
  }

  // Non-bundled plugins are enabled by default
  return true;
}

}  // namespace quantclaw
