#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_MEMORY_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_MEMORY_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class MemoryHandler : public RpcHandlerModule {
 public:
  explicit MemoryHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "memory"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleMemoryStatus(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleMemorySearch(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
  std::string workspace_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_MEMORY_HPP
