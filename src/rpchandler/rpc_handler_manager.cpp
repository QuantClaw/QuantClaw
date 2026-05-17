// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_manager.hpp"
#include "quantclaw/rpchandler/rpc_handler_module.hpp"
#include "quantclaw/rpchandler/rpc_handler_gateway.hpp"
#include "quantclaw/rpchandler/rpc_handler_config.hpp"
#include "quantclaw/rpchandler/rpc_handler_agent.hpp"
#include "quantclaw/rpchandler/rpc_handler_session.hpp"
#include "quantclaw/rpchandler/rpc_handler_channel.hpp"
#include "quantclaw/rpchandler/rpc_handler_skill.hpp"
#include "quantclaw/rpchandler/rpc_handler_cron.hpp"
#include "quantclaw/rpchandler/rpc_handler_memory.hpp"
#include "quantclaw/rpchandler/rpc_handler_exec.hpp"
#include "quantclaw/rpchandler/rpc_handler_model.hpp"
#include "quantclaw/rpchandler/rpc_handler_plugin.hpp"
#include "quantclaw/rpchandler/rpc_handler_queue.hpp"
#include "quantclaw/rpchandler/rpc_handler_openclaw.hpp"
#include "quantclaw/rpchandler/rpc_handler_ui_compat.hpp"

namespace quantclaw::gateway {

RpcHandlerManager::RpcHandlerManager(GatewayServer& server, std::shared_ptr<spdlog::logger> logger)
    : server_(server), logger_(std::move(logger)) {
  context_.server = &server;
  context_.logger = logger_;
}

RpcHandlerManager::~RpcHandlerManager() = default;

// === Builder-style dependency injection ===

RpcHandlerManager& RpcHandlerManager::WithSessionManager(std::shared_ptr<SessionManager> session_manager) {
  context_.session_manager = std::move(session_manager);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithAgentLoop(std::shared_ptr<AgentLoop> agent_loop) {
  context_.agent_loop = std::move(agent_loop);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithPromptBuilder(std::shared_ptr<PromptBuilder> prompt_builder) {
  context_.prompt_builder = std::move(prompt_builder);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithToolRegistry(std::shared_ptr<ToolRegistry> tool_registry) {
  context_.tool_registry = std::move(tool_registry);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithConfig(const QuantClawConfig& config) {
  context_.config = &config;
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithReloadFn(std::function<void()> reload_fn) {
  context_.reload_fn = std::move(reload_fn);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithProviderRegistry(std::shared_ptr<ProviderRegistry> provider_registry) {
  context_.provider_registry = std::move(provider_registry);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithSkillLoaderMeta(std::shared_ptr<SkillLoaderMeta> skill_loader_meta) {
  context_.skill_loader_meta = std::move(skill_loader_meta);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithCronScheduler(std::shared_ptr<CronScheduler> cron_scheduler) {
  context_.cron_scheduler = std::move(cron_scheduler);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithExecApprovalManager(std::shared_ptr<ExecApprovalManager> exec_approval_mgr) {
  context_.exec_approval_mgr = std::move(exec_approval_mgr);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithPluginSystem(PluginSystem* plugin_system) {
  context_.plugin_system = plugin_system;
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithCommandQueue(CommandQueue* command_queue) {
  context_.command_queue = command_queue;
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithLogFilePath(std::string log_file_path) {
  context_.log_file_path = std::move(log_file_path);
  return *this;
}

RpcHandlerManager& RpcHandlerManager::WithRunningAdaptersFn(std::function<std::vector<std::string>()> running_adapters_fn) {
  context_.running_adapters_fn = std::move(running_adapters_fn);
  return *this;
}

void RpcHandlerManager::SetContext(HandlerContext ctx) {
  context_ = std::move(ctx);
  // Ensure infrastructure fields are set
  context_.server = &server_;
  context_.logger = logger_;
}

void RpcHandlerManager::RegisterAll() {
  // Validate context has minimum required dependencies
  context_.Validate();

  // Gateway handlers (health, status)
  RegisterModule(std::make_unique<GatewayHandler>(
      context_));

  // Config handlers (get, set, reload)
  RegisterModule(std::make_unique<ConfigHandler>(
      context_));

  // Agent handlers (request, stop, chain.execute)
  context_.ValidateForAgent();
  RegisterModule(std::make_unique<AgentHandler>(
      context_));

  // Session handlers (list, history, delete, reset, patch, compact, preview)
  context_.ValidateForSession();
  RegisterModule(std::make_unique<SessionHandler>(
      context_));

  // Channel handlers (list, status, logout, agents.list)
  RegisterModule(std::make_unique<ChannelHandler>(
      context_));

  // Skill handlers (status, install, update) - conditional
  if (context_.skill_loader_meta) {
    context_.ValidateForSkills();
    RegisterModule(std::make_unique<SkillHandler>(
        context_));
  }

  // Cron handlers (list, add, remove, update, run, runs, status) - conditional
  if (context_.cron_scheduler) {
    context_.ValidateForCron();
    RegisterModule(std::make_unique<CronHandler>(
        context_));
  }

  // Memory handlers (status, search)
  RegisterModule(std::make_unique<MemoryHandler>(
      context_));

  // ExecApproval handlers (request, get) - conditional
  if (context_.exec_approval_mgr) {
    context_.ValidateForExecApproval();
    RegisterModule(std::make_unique<ExecApprovalHandler>(
        context_));
  }

  // Model handlers (set)
  RegisterModule(std::make_unique<ModelHandler>(
      context_));

  // Plugin handlers (list, tools, call_tool, services, providers, commands, gateway) - conditional
  if (context_.plugin_system) {
    context_.ValidateForPlugins();
    RegisterModule(std::make_unique<PluginHandler>(
        context_));
  }

  // Queue handlers (status, configure, cancel, abort) - conditional
  if (context_.command_queue) {
    context_.ValidateForQueue();
    RegisterModule(std::make_unique<QueueHandler>(
        context_));
  }

  // OpenClaw compat handlers (connect, chat.send/history/abort, etc.)
  context_.ValidateForAgent();
  RegisterModule(std::make_unique<OpenClawCompatHandler>(
      context_));

  // UI compat handlers (node.list, device.pair.list, logs.tail, usage.cost, etc.)
  RegisterModule(std::make_unique<UiCompatHandler>(
      context_));
}

void RpcHandlerManager::RegisterModule(std::unique_ptr<RpcHandlerModule> module) {
  module->RegisterHandlers(server_);
  logger_->info("Registered handler module: {}", module->Name());
  std::lock_guard<std::mutex> lock(handler_modules_mutex_);
  handler_modules_.push_back(std::move(module));
}

}  // namespace quantclaw::gateway
