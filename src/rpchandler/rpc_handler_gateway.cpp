// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_gateway.hpp"

#include "quantclaw/constants.hpp"
#include "quantclaw/session/session_manager.hpp"

namespace quantclaw::gateway {

GatewayHandler::GatewayHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void GatewayHandler::RegisterHandlers(GatewayServer& /*server*/) {
  ctx_.server->RegisterHandler(
      methods::kGatewayHealth,
      [this](const nlohmann::json& /*params*/,
             ClientConnection& client) -> nlohmann::json {
        return HandleGatewayHealth({}, client);
      });

  ctx_.server->RegisterHandler(
      methods::kGatewayStatus,
      [this](const nlohmann::json& /*params*/,
             ClientConnection& client) -> nlohmann::json {
        return HandleGatewayStatus({}, client);
      });
}

nlohmann::json GatewayHandler::HandleGatewayHealth(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"status", "ok"},
          {"uptime", ctx_.server->GetUptimeSeconds()},
          {"version", quantclaw::kVersion}};
}

nlohmann::json GatewayHandler::HandleGatewayStatus(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  auto sessions = ctx_.session_manager->ListSessions();
  return {{"running", true},
          {"port", ctx_.server->GetPort()},
          {"connections", ctx_.server->GetConnectionCount()},
          {"uptime", ctx_.server->GetUptimeSeconds()},
          {"sessions", sessions.size()},
          {"version", quantclaw::kVersion}};
}

}  // namespace quantclaw::gateway
