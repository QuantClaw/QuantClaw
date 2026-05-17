#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_UI_COMPAT_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_UI_COMPAT_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class UiCompatHandler : public RpcHandlerModule {
 public:
  explicit UiCompatHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "ui_compat"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  const HandlerContext& ctx_;
  nlohmann::json HandleAgentIdentityGet(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleNodeList(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleDevicePairList(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleLogsTail(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleConfigSchema(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleSessionsUsage(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleUsageCost(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleSessionsUsageTimeseries(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleSessionsUsageLogs(const nlohmann::json& params, ClientConnection& client);
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_UI_COMPAT_HPP
