#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODULE_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODULE_HPP

#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include <string>

namespace quantclaw::gateway {

// Handler 模块基类：每个功能域实现一个子类，在 RegisterHandlers 中完成所有 handler 注册
class RpcHandlerModule {
 public:
  virtual ~RpcHandlerModule() = default;

  // 模块名称（用于日志）
  [[nodiscard]] virtual std::string Name() const = 0;

  // 将本模块的所有 handler 注册到 server
  virtual void RegisterHandlers(GatewayServer& server) = 0;
};

}  // namespace quantclaw::gateway

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_MODULE_HPP
