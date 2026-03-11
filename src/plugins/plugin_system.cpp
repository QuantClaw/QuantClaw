// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/plugins/plugin_system.hpp"

#include <filesystem>

namespace quantclaw {

namespace {

std::string find_sidecar_script() {
  // Look for sidecar entry point in standard locations
  const char* home = std::getenv("HOME");
  std::string home_str = home ? home : "/tmp";

  std::vector<std::string> candidates = {
      home_str + "/.quantclaw/sidecar/index.js",
      home_str + "/.quantclaw/sidecar/dist/index.js",
      "/usr/lib/quantclaw/sidecar/index.js",
      "/usr/local/lib/quantclaw/sidecar/index.js",
  };

  for (const auto& path : candidates) {
    if (std::filesystem::exists(path))
      return path;
  }
  return "";
}

}  // namespace

PluginSystem::PluginSystem(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger), registry_(logger), hooks_(logger) {}

PluginSystem::~PluginSystem() {
  Shutdown();
}

bool PluginSystem::Initialize(const QuantClawConfig& config,
                              const std::filesystem::path& workspace_dir) {
  // Step 1: Discover and register plugins from manifests
  registry_.Discover(config, workspace_dir);

  auto enabled = registry_.EnabledPluginIds();
  if (enabled.empty()) {
    logger_->info("No enabled plugins found, skipping sidecar");
    return true;
  }

  // Step 2: Start sidecar if there are enabled plugins with JS code
  std::string script = find_sidecar_script();
  if (script.empty()) {
    logger_->info(
        "No sidecar script found, plugins will run in manifest-only mode");
    return true;
  }

  sidecar_ = std::make_shared<SidecarManager>(logger_);
  hooks_.SetSidecar(sidecar_);

  SidecarManager::Options opts;
  opts.sidecar_script = script;
  opts.plugin_config = {
      {"enabled_plugins", enabled},
      {"workspace_dir", workspace_dir.string()},
      {"plugins", config.plugins_config},
  };

  if (!sidecar_->Start(opts)) {
    logger_->error("Failed to start sidecar, plugins will be unavailable");
    sidecar_.reset();
    return false;
  }

  // Update plugin records with capabilities from sidecar
  auto list_resp = sidecar_->Call("plugin.list", {});
  if (list_resp.ok) {
    registry_.UpdateFromSidecar(list_resp.result);
  } else {
    logger_->warn("Failed to get plugin list from sidecar: {}",
                  list_resp.error);
  }

  // Fire gateway_start hook
  hooks_.Fire(hooks::kGatewayStart, {{"timestamp", std::time(nullptr)}});

  logger_->info("Plugin system initialized ({} plugins, sidecar running)",
                enabled.size());
  return true;
}

void PluginSystem::Shutdown() {
  if (sidecar_ && sidecar_->IsRunning()) {
    hooks_.Fire(hooks::kGatewayStop, {{"timestamp", std::time(nullptr)}});
    sidecar_->Stop();
  }
  sidecar_.reset();
}

bool PluginSystem::Reload(const QuantClawConfig& config,
                          const std::filesystem::path& workspace_dir) {
  registry_.Discover(config, workspace_dir);

  if (sidecar_ && sidecar_->IsRunning()) {
    return sidecar_->Reload();
  }
  return true;
}

nlohmann::json PluginSystem::CallTool(const std::string& tool_name,
                                      const nlohmann::json& args) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->Call("plugin.call_tool",
                             {{"toolName", tool_name}, {"args", args}});
  if (!resp.ok) {
    return {{"error", resp.error}};
  }
  return resp.result;
}

nlohmann::json PluginSystem::GetToolSchemas() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.tools", {});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::ListSidecarPlugins() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.list", {});
  if (!resp.ok)
    return nlohmann::json::array();
  // plugin.list returns {plugins: [...]}
  if (resp.result.is_object() && resp.result.contains("plugins")) {
    return resp.result["plugins"];
  }
  return resp.result;
}

nlohmann::json
PluginSystem::HandleHttp(const std::string& method, const std::string& path,
                         const nlohmann::json& body,
                         const std::map<std::string, std::string>& headers) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}, {"status", 503}};
  }

  nlohmann::json headers_json;
  for (const auto& [k, v] : headers) {
    headers_json[k] = v;
  }

  auto resp = sidecar_->Call("plugin.http", {
                                                {"method", method},
                                                {"path", path},
                                                {"body", body},
                                                {"headers", headers_json},
                                            });

  if (!resp.ok) {
    return {{"error", resp.error}, {"status", 502}};
  }
  return resp.result;
}

nlohmann::json PluginSystem::HandleCli(const std::string& command,
                                       const std::vector<std::string>& args) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->Call("plugin.cli", {
                                               {"command", command},
                                               {"args", args},
                                           });
  return resp.ok ? resp.result : nlohmann::json{{"error", resp.error}};
}

nlohmann::json PluginSystem::ListServices() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.services", {{"action", "list"}});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::StartService(const std::string& service_id) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->Call("plugin.services",
                             {{"action", "start"}, {"serviceId", service_id}});
  return resp.ok ? resp.result : nlohmann::json{{"error", resp.error}};
}

nlohmann::json PluginSystem::StopService(const std::string& service_id) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->Call("plugin.services",
                             {{"action", "stop"}, {"serviceId", service_id}});
  return resp.ok ? resp.result : nlohmann::json{{"error", resp.error}};
}

nlohmann::json PluginSystem::ListProviders() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.providers", {});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::ListCommands() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.commands", {{"action", "list"}});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::ExecuteCommand(const std::string& command,
                                            const nlohmann::json& args) {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->Call(
      "plugin.commands",
      {{"action", "execute"}, {"command", command}, {"args", args}});
  return resp.ok ? resp.result : nlohmann::json{{"error", resp.error}};
}

nlohmann::json PluginSystem::ListGatewayMethods() {
  if (!sidecar_ || !sidecar_->IsRunning()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->Call("plugin.gateway_methods", {});
  return resp.ok ? resp.result : nlohmann::json::array();
}

bool PluginSystem::IsSidecarRunning() const {
  return sidecar_ && sidecar_->IsRunning();
}

}  // namespace quantclaw
