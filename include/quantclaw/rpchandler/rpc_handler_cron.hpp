#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_CRON_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_CRON_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class CronHandler : public RpcHandlerModule {
 public:
  explicit CronHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "cron"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleCronList(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleCronAdd(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleCronRemove(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleCronUpdate(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleCronRun(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleCronRuns(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_CRON_HPP
