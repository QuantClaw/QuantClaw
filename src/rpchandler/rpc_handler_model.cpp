// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_model.hpp"

#include "quantclaw/core/agent_loop.hpp"

namespace quantclaw::gateway {

ModelHandler::ModelHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void ModelHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- models.set ---
  ctx_.server->RegisterHandler(methods::kModelsSet,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleModelsSet(params, client);
      });
}

nlohmann::json ModelHandler::HandleModelsSet(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string model = params.value("model", "");
  if (model.empty()) {
    throw std::runtime_error("model is required");
  }
  ctx_.agent_loop->SetModel(model);
  return {{"ok", true}, {"model", model}};
}

}  // namespace quantclaw::gateway
