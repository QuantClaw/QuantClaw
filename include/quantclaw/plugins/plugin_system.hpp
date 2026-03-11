// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "quantclaw/config.hpp"
#include "quantclaw/plugins/hook_manager.hpp"
#include "quantclaw/plugins/plugin_registry.hpp"
#include "quantclaw/plugins/sidecar_manager.hpp"

namespace quantclaw {

// Top-level facade that ties together the plugin registry, sidecar, and hooks.
// Used by Gateway on startup to discover plugins, start the sidecar,
// and wire up hook dispatch.
class PluginSystem {
 public:
  explicit PluginSystem(std::shared_ptr<spdlog::logger> logger);
  ~PluginSystem();

  PluginSystem(const PluginSystem&) = delete;
  PluginSystem& operator=(const PluginSystem&) = delete;

  // Initialize: discover plugins and optionally start sidecar
  bool Initialize(const QuantClawConfig& config,
                  const std::filesystem::path& workspace_dir);

  // Shutdown sidecar and clean up
  void Shutdown();

  // Reload plugins (re-discover + SIGHUP sidecar)
  bool Reload(const QuantClawConfig& config,
              const std::filesystem::path& workspace_dir);

  // Access components
  PluginRegistry& Registry() {
    return registry_;
  }
  const PluginRegistry& Registry() const {
    return registry_;
  }
  HookManager& Hooks() {
    return hooks_;
  }
  SidecarManager* Sidecar() {
    return sidecar_.get();
  }

  // Convenience: call a plugin tool via sidecar
  nlohmann::json CallTool(const std::string& tool_name,
                          const nlohmann::json& args);

  // Convenience: get tool schemas from sidecar
  nlohmann::json GetToolSchemas();

  // Convenience: list loaded plugins from sidecar
  nlohmann::json ListSidecarPlugins();

  // Handle an HTTP request through sidecar plugin routes
  nlohmann::json HandleHttp(const std::string& method, const std::string& path,
                            const nlohmann::json& body,
                            const std::map<std::string, std::string>& headers);

  // Route a CLI command through sidecar
  nlohmann::json HandleCli(const std::string& command,
                           const std::vector<std::string>& args);

  // Service management via sidecar
  nlohmann::json ListServices();
  nlohmann::json StartService(const std::string& service_id);
  nlohmann::json StopService(const std::string& service_id);

  // Provider listing via sidecar
  nlohmann::json ListProviders();

  // Command listing and execution via sidecar
  nlohmann::json ListCommands();
  nlohmann::json ExecuteCommand(const std::string& command,
                                const nlohmann::json& args);

  // Gateway method listing via sidecar
  nlohmann::json ListGatewayMethods();

  bool IsSidecarRunning() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
  PluginRegistry registry_;
  HookManager hooks_;
  std::shared_ptr<SidecarManager> sidecar_;
};

}  // namespace quantclaw
