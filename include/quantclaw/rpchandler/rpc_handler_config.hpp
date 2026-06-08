#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONFIG_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONFIG_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class ConfigHandler : public RpcHandlerModule {
 public:
  explicit ConfigHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "config"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleConfigGet(const nlohmann::json& params,
                                 ClientConnection& client);
  nlohmann::json HandleConfigSet(const nlohmann::json& params,
                                 ClientConnection& client);
  nlohmann::json HandleConfigReload(const nlohmann::json& params,
                                    ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONFIG_HPP
