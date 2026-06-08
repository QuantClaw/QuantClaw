#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_QUEUE_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_QUEUE_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class QueueHandler : public RpcHandlerModule {
 public:
  explicit QueueHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "queue"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  const HandlerContext& ctx_;
  nlohmann::json HandleQueueStatus(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleQueueConfigure(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleQueueCancel(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleQueueAbort(const nlohmann::json& params, ClientConnection& client);
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_QUEUE_HPP
