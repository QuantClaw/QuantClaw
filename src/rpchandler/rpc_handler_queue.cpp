// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_queue.hpp"
#include "quantclaw/gateway/command_queue.hpp"

namespace quantclaw::gateway {

QueueHandler::QueueHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void QueueHandler::RegisterHandlers(GatewayServer& /*server*/) {
  if (!ctx_.command_queue) return;

  // --- queue.status ---
  ctx_.server->RegisterHandler(methods::kQueueStatus,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleQueueStatus(params, client);
      });

  // --- queue.configure ---
  ctx_.server->RegisterHandler(methods::kQueueConfigure,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleQueueConfigure(params, client);
      });

  // --- queue.cancel ---
  ctx_.server->RegisterHandler(methods::kQueueCancel,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleQueueCancel(params, client);
      });

  // --- queue.abort ---
  ctx_.server->RegisterHandler(methods::kQueueAbort,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleQueueAbort(params, client);
      });
}

nlohmann::json QueueHandler::HandleQueueStatus(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (!session_key.empty()) {
    return ctx_.command_queue->SessionQueueStatus(session_key);
  }
  return ctx_.command_queue->GlobalStatus();
}

nlohmann::json QueueHandler::HandleQueueConfigure(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    // Global config update
    auto new_config = QueueConfig::FromJson(params);
    ctx_.command_queue->SetConfig(new_config);
    return {{"ok", true}, {"scope", "global"}};
  }
  // Per-session config
  auto mode = QueueModeFromString(params.value("mode", "collect"));
  int debounce = params.value("debounceMs", -1);
  int cap = params.value("cap", -1);
  std::string drop = params.value("drop", "");
  ctx_.command_queue->ConfigureSession(session_key, mode, debounce, cap, drop);
  return {{"ok", true}, {"scope", "session"}, {"sessionKey", session_key}};
}

nlohmann::json QueueHandler::HandleQueueCancel(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string command_id = params.value("commandId", "");
  if (command_id.empty()) {
    throw std::runtime_error("commandId is required");
  }
  bool cancelled = ctx_.command_queue->Cancel(command_id);
  return {{"ok", cancelled}, {"commandId", command_id}};
}

nlohmann::json QueueHandler::HandleQueueAbort(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }
  bool aborted = ctx_.command_queue->AbortSession(session_key);
  return {{"ok", aborted}, {"sessionKey", session_key}};
}

}  // namespace quantclaw::gateway
