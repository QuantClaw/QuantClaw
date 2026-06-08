#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_SESSION_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_SESSION_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class SessionHandler : public RpcHandlerModule {
 public:
  explicit SessionHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "session"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleSessionsList(const nlohmann::json& params,
                                    ClientConnection& client);
  nlohmann::json HandleSessionsHistory(const nlohmann::json& params,
                                       ClientConnection& client);
  nlohmann::json HandleSessionsDelete(const nlohmann::json& params,
                                      ClientConnection& client);
  nlohmann::json HandleSessionsReset(const nlohmann::json& params,
                                     ClientConnection& client);
  nlohmann::json HandleSessionsPatch(const nlohmann::json& params,
                                     ClientConnection& client);
  nlohmann::json HandleSessionsCompact(const nlohmann::json& params,
                                       ClientConnection& client);
  nlohmann::json HandleSessionsPreview(const nlohmann::json& params,
                                       ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_SESSION_HPP
