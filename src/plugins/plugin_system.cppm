// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.plugins.plugin_system;

import std;
import nlohmann.json;
import quantclaw.config;
import quantclaw.plugins.hook_manager;
import quantclaw.plugins.plugin_registry;
import quantclaw.plugins.sidecar_manager;

namespace spdlog {
class logger;
}

export namespace quantclaw {

class PluginSystem {
 public:
  explicit PluginSystem(std::shared_ptr<spdlog::logger> logger);
  ~PluginSystem();

  PluginSystem(const PluginSystem&) = delete;
  PluginSystem& operator=(const PluginSystem&) = delete;

  bool Initialize(const QuantClawConfig& config,
                  const std::filesystem::path& workspace_dir);
  void Shutdown();
  bool Reload(const QuantClawConfig& config,
              const std::filesystem::path& workspace_dir);

  PluginRegistry& Registry() { return registry_; }
  const PluginRegistry& Registry() const { return registry_; }
  HookManager& Hooks() { return hooks_; }
  SidecarManager* Sidecar() { return sidecar_.get(); }

  nlohmann::json CallTool(const std::string& tool_name,
                          const nlohmann::json& args);
  nlohmann::json GetToolSchemas();
  nlohmann::json ListSidecarPlugins();
  nlohmann::json HandleHttp(const std::string& method, const std::string& path,
                            const nlohmann::json& body,
                            const std::map<std::string, std::string>& headers);
  nlohmann::json HandleCli(const std::string& command,
                           const std::vector<std::string>& args);

  nlohmann::json ListServices();
  nlohmann::json StartService(const std::string& service_id);
  nlohmann::json StopService(const std::string& service_id);

  nlohmann::json ListProviders();
  nlohmann::json ListCommands();
  nlohmann::json ExecuteCommand(const std::string& command,
                                const nlohmann::json& args);
  nlohmann::json ListGatewayMethods();

  bool IsSidecarRunning() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
  PluginRegistry registry_;
  HookManager hooks_;
  std::shared_ptr<SidecarManager> sidecar_;
};

}  // namespace quantclaw