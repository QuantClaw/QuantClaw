#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_EXEC_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_EXEC_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class ExecApprovalHandler : public RpcHandlerModule {
 public:
  explicit ExecApprovalHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "exec"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleExecApprovalRequest(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandleExecApprovalsGet(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_EXEC_HPP
