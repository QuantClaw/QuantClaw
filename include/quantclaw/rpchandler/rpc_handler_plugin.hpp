#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_PLUGIN_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_PLUGIN_HPP

#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"

namespace quantclaw::gateway {

class PluginHandler : public RpcHandlerModule {
 public:
  explicit PluginHandler(const HandlerContext& ctx);

  [[nodiscard]] std::string Name() const override { return "plugin"; }
  void RegisterHandlers(GatewayServer& server) override;

 private:
  nlohmann::json HandlePluginsList(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsTools(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsCallTool(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsServices(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsProviders(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsCommands(const nlohmann::json& params, ClientConnection& client);
  nlohmann::json HandlePluginsGateway(const nlohmann::json& params, ClientConnection& client);

  const HandlerContext& ctx_;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_PLUGIN_HPP
