#pragma once

#include "quantclaw/plugins/hook_manager.hpp"
#include "quantclaw/plugins/plugin_registry.hpp"
#include "quantclaw/plugins/sidecar_manager.hpp"
#include "quantclaw/config.hpp"
#include <memory>

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
  bool initialize(const QuantClawConfig& config,
                  const std::filesystem::path& workspace_dir);

  // Shutdown sidecar and clean up
  void shutdown();

  // Reload plugins (re-discover + SIGHUP sidecar)
  bool reload(const QuantClawConfig& config,
              const std::filesystem::path& workspace_dir);

  // Access components
  PluginRegistry& registry() { return registry_; }
  const PluginRegistry& registry() const { return registry_; }
  HookManager& hooks() { return hooks_; }
  SidecarManager* sidecar() { return sidecar_.get(); }

  // Convenience: call a plugin tool via sidecar
  nlohmann::json call_tool(const std::string& tool_name,
                           const nlohmann::json& args);

  // Convenience: get tool schemas from sidecar
  nlohmann::json get_tool_schemas();

  // Convenience: list loaded plugins from sidecar
  nlohmann::json list_sidecar_plugins();

  // Handle an HTTP request through sidecar plugin routes
  nlohmann::json handle_http(const std::string& method,
                             const std::string& path,
                             const nlohmann::json& body,
                             const std::map<std::string, std::string>& headers);

  // Route a CLI command through sidecar
  nlohmann::json handle_cli(const std::string& command,
                            const std::vector<std::string>& args);

  bool is_sidecar_running() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
  PluginRegistry registry_;
  HookManager hooks_;
  std::shared_ptr<SidecarManager> sidecar_;
};

}  // namespace quantclaw
