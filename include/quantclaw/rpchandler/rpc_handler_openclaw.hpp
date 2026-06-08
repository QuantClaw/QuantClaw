#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_OPENCLAW_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_OPENCLAW_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class OpenClawCompatHandler : public RpcHandlerModule {
 public:
  explicit OpenClawCompatHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "openclaw"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleChatSend(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleChatHistory(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleChatAbort(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleHealth(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleStatus(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleModelsList(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleToolsCatalog(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_OPENCLAW_HPP
