// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_config.hpp"

#include "quantclaw/config.hpp"
#include <filesystem>

namespace quantclaw::gateway {

ConfigHandler::ConfigHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void ConfigHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- config.get ---
  ctx_.server->RegisterHandler(
      methods::kConfigGet,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleConfigGet(params, client);
      });

  // --- config.set ---
  ctx_.server->RegisterHandler(
      methods::kConfigSet,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleConfigSet(params, client);
      });

  // --- config.reload / config.apply ---
  if (ctx_.reload_fn) {
    ctx_.server->RegisterHandler(
        methods::kConfigReload,
        [this](const nlohmann::json& params,
               ClientConnection& client) -> nlohmann::json {
          return HandleConfigReload(params, client);
        });
    ctx_.server->RegisterHandler("config.apply",
                           [this](const nlohmann::json& params,
                                  ClientConnection& client) -> nlohmann::json {
                             return HandleConfigReload(params, client);
                           });
  }
}

nlohmann::json ConfigHandler::HandleConfigGet(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string path_param = params.value("path", "");

  // Build the full config object that the UI config form expects
  nlohmann::json full_config = {
      {"agent",
       {{"model", ctx_.config->agent.model},
        {"maxIterations", ctx_.config->agent.max_iterations},
        {"temperature", ctx_.config->agent.temperature},
        {"maxTokens", ctx_.config->agent.max_tokens},
        {"contextWindow", ctx_.config->agent.context_window},
        {"thinking", ctx_.config->agent.thinking},
        {"autoCompact", ctx_.config->agent.auto_compact}}},
      {"gateway",
       {{"port", ctx_.config->gateway.port}, {"bind", ctx_.config->gateway.bind}}}};

  if (!path_param.empty()) {
    // Dot-path lookup for legacy callers
    if (path_param == "gateway.port")
      return ctx_.config->gateway.port;
    if (path_param == "gateway.bind")
      return ctx_.config->gateway.bind;
    if (path_param == "agent.model")
      return ctx_.config->agent.model;
    if (path_param == "agent.maxIterations")
      return ctx_.config->agent.max_iterations;
    if (path_param == "agent.temperature")
      return ctx_.config->agent.temperature;
    throw std::runtime_error("Unknown config path: " + path_param);
  }

  // Return ConfigSnapshot shape expected by the UI
  auto config_path = QuantClawConfig::DefaultConfigPath();
  bool exists = std::filesystem::exists(config_path);
  std::string raw_str = full_config.dump(2);

  return {{"path", config_path},   {"exists", exists},
          {"raw", raw_str},        {"hash", ""},
          {"parsed", full_config}, {"valid", true},
          {"config", full_config}, {"issues", nlohmann::json::array()}};
}

nlohmann::json ConfigHandler::HandleConfigSet(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string path = params.value("path", "");
  if (path.empty()) {
    throw std::runtime_error("path is required");
  }
  if (!params.contains("value")) {
    throw std::runtime_error("value is required");
  }

  auto config_file = QuantClawConfig::DefaultConfigPath();
  QuantClawConfig::SetValue(config_file, path, params["value"]);

  // Trigger hot-reload so the running server picks up the change
  if (ctx_.reload_fn) {
    ctx_.reload_fn();
  }

  return {{"ok", true}, {"path", path}};
}

nlohmann::json ConfigHandler::HandleConfigReload(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  if (ctx_.reload_fn) {
    ctx_.reload_fn();
  }
  return {{"ok", true}};
}

}  // namespace quantclaw::gateway
