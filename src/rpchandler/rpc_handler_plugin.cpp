// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_plugin.hpp"

#include "quantclaw/plugins/plugin_system.hpp"

namespace quantclaw::gateway {

PluginHandler::PluginHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void PluginHandler::RegisterHandlers(GatewayServer& /*server*/) {
  if (!ctx_.plugin_system) return;

  // plugins.list
  ctx_.server->RegisterHandler(methods::kPluginsList,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsList(params, client);
      });

  // plugins.tools
  ctx_.server->RegisterHandler(methods::kPluginsTools,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsTools(params, client);
      });

  // plugins.call_tool
  ctx_.server->RegisterHandler(methods::kPluginsCallTool,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsCallTool(params, client);
      });

  // plugins.services
  ctx_.server->RegisterHandler(methods::kPluginsServices,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsServices(params, client);
      });

  // plugins.providers
  ctx_.server->RegisterHandler(methods::kPluginsProviders,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsProviders(params, client);
      });

  // plugins.commands
  ctx_.server->RegisterHandler(methods::kPluginsCommands,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsCommands(params, client);
      });

  // plugins.gateway
  ctx_.server->RegisterHandler(methods::kPluginsGateway,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandlePluginsGateway(params, client);
      });
}

nlohmann::json PluginHandler::HandlePluginsList(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"plugins", ctx_.plugin_system->Registry().ToJson()}};
}

nlohmann::json PluginHandler::HandlePluginsTools(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"tools", ctx_.plugin_system->GetToolSchemas()}};
}

nlohmann::json PluginHandler::HandlePluginsCallTool(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string name = params.value("toolName", "");
  if (name.empty())
    throw std::runtime_error("toolName is required");
  auto args = params.value("args", nlohmann::json::object());
  return ctx_.plugin_system->CallTool(name, args);
}

nlohmann::json PluginHandler::HandlePluginsServices(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string action = params.value("action", "list");
  if (action == "start") {
    return ctx_.plugin_system->StartService(params.value("serviceId", ""));
  }
  if (action == "stop") {
    return ctx_.plugin_system->StopService(params.value("serviceId", ""));
  }
  return {{"services", ctx_.plugin_system->ListServices()}};
}

nlohmann::json PluginHandler::HandlePluginsProviders(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"providers", ctx_.plugin_system->ListProviders()}};
}

nlohmann::json PluginHandler::HandlePluginsCommands(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string action = params.value("action", "list");
  if (action == "execute") {
    std::string cmd = params.value("command", "");
    auto args = params.value("args", nlohmann::json::object());
    return ctx_.plugin_system->ExecuteCommand(cmd, args);
  }
  return {{"commands", ctx_.plugin_system->ListCommands()}};
}

nlohmann::json PluginHandler::HandlePluginsGateway(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string action = params.value("action", "list");
  if (action == "list") {
    return {{"methods", ctx_.plugin_system->ListGatewayMethods()}};
  }
  return {{"methods", ctx_.plugin_system->ListGatewayMethods()}};
}

}  // namespace quantclaw::gateway
