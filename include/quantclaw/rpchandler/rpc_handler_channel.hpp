#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_CHANNEL_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_CHANNEL_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class ChannelHandler : public RpcHandlerModule {
 public:
  explicit ChannelHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "channel"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleChannelsList(const nlohmann::json& params,
                                    ClientConnection& client);
  nlohmann::json HandleChannelsStatus(const nlohmann::json& params,
                                      ClientConnection& client);
  nlohmann::json HandleChannelsLogout(const nlohmann::json& params,
                                      ClientConnection& client);
  nlohmann::json HandleAgentsList(const nlohmann::json& params,
                                  ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_CHANNEL_HPP
