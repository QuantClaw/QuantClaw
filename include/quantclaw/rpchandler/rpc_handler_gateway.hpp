#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_GATEWAY_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_GATEWAY_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"
#include <memory>
#include <spdlog/spdlog.h>

namespace quantclaw::gateway {

class GatewayHandler : public RpcHandlerModule {
 public:
  explicit GatewayHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "gateway"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleGatewayHealth(const nlohmann::json& params,
                                     ClientConnection& client);
  nlohmann::json HandleGatewayStatus(const nlohmann::json& params,
                                     ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_GATEWAY_HPP
