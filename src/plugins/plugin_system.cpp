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
    if (std::filesystem::exists(path)) return path;
  }
  return "";
}

}  // namespace

PluginSystem::PluginSystem(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger),
      registry_(logger),
      hooks_(logger) {}

PluginSystem::~PluginSystem() {
  shutdown();
}

bool PluginSystem::initialize(const QuantClawConfig& config,
                              const std::filesystem::path& workspace_dir) {
  // Step 1: Discover and register plugins from manifests
  registry_.discover(config, workspace_dir);

  auto enabled = registry_.enabled_plugin_ids();
  if (enabled.empty()) {
    logger_->info("No enabled plugins found, skipping sidecar");
    return true;
  }

  // Step 2: Start sidecar if there are enabled plugins with JS code
  std::string script = find_sidecar_script();
  if (script.empty()) {
    logger_->info("No sidecar script found, plugins will run in manifest-only mode");
    return true;
  }

  sidecar_ = std::make_shared<SidecarManager>(logger_);
  hooks_.set_sidecar(sidecar_);

  SidecarManager::Options opts;
  opts.sidecar_script = script;
  opts.plugin_config = {
      {"enabled_plugins", enabled},
      {"workspace_dir", workspace_dir.string()},
      {"plugins", config.plugins_config},
  };

  if (!sidecar_->start(opts)) {
    logger_->error("Failed to start sidecar, plugins will be unavailable");
    sidecar_.reset();
    return false;
  }

  // Fire gateway_start hook
  hooks_.fire(hooks::kGatewayStart, {{"timestamp", std::time(nullptr)}});

  logger_->info("Plugin system initialized ({} plugins, sidecar running)",
                enabled.size());
  return true;
}

void PluginSystem::shutdown() {
  if (sidecar_ && sidecar_->is_running()) {
    hooks_.fire(hooks::kGatewayStop, {{"timestamp", std::time(nullptr)}});
    sidecar_->stop();
  }
  sidecar_.reset();
}

bool PluginSystem::reload(const QuantClawConfig& config,
                          const std::filesystem::path& workspace_dir) {
  registry_.discover(config, workspace_dir);

  if (sidecar_ && sidecar_->is_running()) {
    return sidecar_->reload();
  }
  return true;
}

nlohmann::json PluginSystem::call_tool(const std::string& tool_name,
                                       const nlohmann::json& args) {
  if (!sidecar_ || !sidecar_->is_running()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->call("plugin.call_tool",
                             {{"toolName", tool_name}, {"args", args}});
  if (!resp.ok) {
    return {{"error", resp.error}};
  }
  return resp.result;
}

nlohmann::json PluginSystem::get_tool_schemas() {
  if (!sidecar_ || !sidecar_->is_running()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->call("plugin.tools", {});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::list_sidecar_plugins() {
  if (!sidecar_ || !sidecar_->is_running()) {
    return nlohmann::json::array();
  }
  auto resp = sidecar_->call("plugin.list", {});
  return resp.ok ? resp.result : nlohmann::json::array();
}

nlohmann::json PluginSystem::handle_http(
    const std::string& method,
    const std::string& path,
    const nlohmann::json& body,
    const std::map<std::string, std::string>& headers) {
  if (!sidecar_ || !sidecar_->is_running()) {
    return {{"error", "Sidecar not available"}, {"status", 503}};
  }

  nlohmann::json headers_json;
  for (const auto& [k, v] : headers) {
    headers_json[k] = v;
  }

  auto resp = sidecar_->call("plugin.http", {
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

nlohmann::json PluginSystem::handle_cli(
    const std::string& command,
    const std::vector<std::string>& args) {
  if (!sidecar_ || !sidecar_->is_running()) {
    return {{"error", "Sidecar not available"}};
  }
  auto resp = sidecar_->call("plugin.cli", {
      {"command", command},
      {"args", args},
  });
  return resp.ok ? resp.result : nlohmann::json{{"error", resp.error}};
}

bool PluginSystem::is_sidecar_running() const {
  return sidecar_ && sidecar_->is_running();
}

}  // namespace quantclaw
