#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_SKILL_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_SKILL_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class SkillHandler : public RpcHandlerModule {
 public:
  explicit SkillHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "skill"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandleSkillsStatus(const nlohmann::json& params,
                                    ClientConnection& client);
  nlohmann::json HandleSkillsInstall(const nlohmann::json& params,
                                     ClientConnection& client);
  nlohmann::json HandleSkillsUpdate(const nlohmann::json& params,
                                    ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_SKILL_HPP
