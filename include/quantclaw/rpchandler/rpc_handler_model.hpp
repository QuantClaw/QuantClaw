#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODEL_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODEL_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class ModelHandler : public RpcHandlerModule {
 public:
  explicit ModelHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "model"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleModelsSet(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODEL_HPP
