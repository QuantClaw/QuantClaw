#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_AGENT_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_AGENT_HPP

#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class AgentHandler : public RpcHandlerModule {
 public:
  explicit AgentHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "agent"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  struct AgentRequestResult {
    std::string session_key;
    std::string final_response;
    std::string error_message;
  };

  AgentRequestResult ExecuteAgentRequest(
      const nlohmann::json& params,
      ClientConnection& client,
      AgentEventCallback event_callback);

  nlohmann::json HandleAgentRequest(const nlohmann::json& params,
                                    ClientConnection& client);
  nlohmann::json HandleAgentStop(const nlohmann::json& params,
                                 ClientConnection& client);
  nlohmann::json HandleChainExecute(const nlohmann::json& params,
                                    ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_AGENT_HPP
