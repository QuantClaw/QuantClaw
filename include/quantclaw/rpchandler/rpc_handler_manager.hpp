// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_MANAGER_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_MANAGER_HPP

#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/rpchandler/rpc_handler_context.hpp"
#include <memory>
#include <spdlog/spdlog.h>
#include <vector>

namespace quantclaw {
namespace gateway {

class RpcHandlerModule;

// Manages the lifecycle and registration of all RPC handler modules
// Uses HandlerContext pattern for clean dependency injection
class RpcHandlerManager {
 public:
  explicit RpcHandlerManager(GatewayServer& server, std::shared_ptr<spdlog::logger> logger);
  ~RpcHandlerManager();

  // === Builder-style dependency injection ===
  // These methods return *this for method chaining
  RpcHandlerManager& WithSessionManager(std::shared_ptr<SessionManager> session_manager);
  RpcHandlerManager& WithAgentLoop(std::shared_ptr<AgentLoop> agent_loop);
  RpcHandlerManager& WithPromptBuilder(std::shared_ptr<PromptBuilder> prompt_builder);
  RpcHandlerManager& WithToolRegistry(std::shared_ptr<ToolRegistry> tool_registry);
  RpcHandlerManager& WithConfig(const QuantClawConfig& config);
  RpcHandlerManager& WithReloadFn(std::function<void()> reload_fn);
  RpcHandlerManager& WithProviderRegistry(std::shared_ptr<ProviderRegistry> provider_registry);
  RpcHandlerManager& WithSkillLoaderMeta(std::shared_ptr<SkillLoaderMeta> skill_loader_meta);
  RpcHandlerManager& WithCronScheduler(std::shared_ptr<CronScheduler> cron_scheduler);
  RpcHandlerManager& WithExecApprovalManager(std::shared_ptr<ExecApprovalManager> exec_approval_mgr);
  RpcHandlerManager& WithPluginSystem(PluginSystem* plugin_system);
  RpcHandlerManager& WithCommandQueue(CommandQueue* command_queue);
  RpcHandlerManager& WithLogFilePath(std::string log_file_path);
  RpcHandlerManager& WithRunningAdaptersFn(std::function<std::vector<std::string>()> running_adapters_fn);

  // Set all dependencies at once (alternative to builder pattern)
  void SetContext(HandlerContext ctx);

  // Register all configured handler modules to the server
  void RegisterAll();

 private:
  // Helper method to register a single handler module
  void RegisterModule(std::unique_ptr<RpcHandlerModule> module);

  GatewayServer& server_;
  std::shared_ptr<spdlog::logger> logger_;
  HandlerContext context_;

  // Owned handler modules (keeps them alive for the lifetime of the manager)
  std::vector<std::unique_ptr<RpcHandlerModule>> handler_modules_;
  std::mutex handler_modules_mutex_;
};

}  // namespace gateway
}  // namespace quantclaw

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_MANAGER_HPP
