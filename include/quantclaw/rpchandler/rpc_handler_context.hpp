// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONTEXT_HPP
#define QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONTEXT_HPP

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace quantclaw {
class AgentLoop;
class PromptBuilder;
class ToolRegistry;
class SessionManager;
class ProviderRegistry;
class SkillLoaderMeta;
class CronScheduler;
class ExecApprovalManager;
class PluginSystem;
struct QuantClawConfig;

namespace gateway {
class GatewayServer;
class CommandQueue;

// HandlerContext encapsulates all dependencies for RPC handlers
// This simplifies constructor signatures and makes dependencies explicit
struct HandlerContext {
  // === Core Business Services ===
  std::shared_ptr<AgentLoop> agent_loop;
  std::shared_ptr<SessionManager> session_manager;
  std::shared_ptr<PromptBuilder> prompt_builder;
  std::shared_ptr<ToolRegistry> tool_registry;
  std::shared_ptr<ProviderRegistry> provider_registry;
  
  // === Configuration & Runtime ===
  const QuantClawConfig* config = nullptr;
  std::function<void()> reload_fn;
  std::function<std::vector<std::string>()> running_adapters_fn;
  
  // === Optional Services (may be nullptr) ===
  std::shared_ptr<SkillLoaderMeta> skill_loader_meta;
  std::shared_ptr<CronScheduler> cron_scheduler;
  std::shared_ptr<ExecApprovalManager> exec_approval_mgr;
  PluginSystem* plugin_system = nullptr;
  CommandQueue* command_queue = nullptr;
  
  // === Infrastructure ===
  GatewayServer* server = nullptr;
  std::shared_ptr<spdlog::logger> logger;
  std::string log_file_path;
  
  // Validate that all required dependencies are set
  void Validate() const {
    if (!config) {
      throw std::runtime_error("HandlerContext: config is required");
    }
    if (!server) {
      throw std::runtime_error("HandlerContext: server is required");
    }
    if (!logger) {
      throw std::runtime_error("HandlerContext: logger is required");
    }
  }
  
  // Validate dependencies for specific handler groups
  void ValidateForAgent() const {
    Validate();
    if (!agent_loop) {
      throw std::runtime_error("HandlerContext: agent_loop is required for agent handlers");
    }
    if (!session_manager) {
      throw std::runtime_error("HandlerContext: session_manager is required for agent handlers");
    }
    if (!prompt_builder) {
      throw std::runtime_error("HandlerContext: prompt_builder is required for agent handlers");
    }
    if (!tool_registry) {
      throw std::runtime_error("HandlerContext: tool_registry is required for agent handlers");
    }
  }
  
  void ValidateForSession() const {
    Validate();
    if (!session_manager) {
      throw std::runtime_error("HandlerContext: session_manager is required for session handlers");
    }
    if (!agent_loop) {
      throw std::runtime_error("HandlerContext: agent_loop is required for session handlers");
    }
  }
  
  void ValidateForCron() const {
    Validate();
    if (!cron_scheduler) {
      throw std::runtime_error("HandlerContext: cron_scheduler is required for cron handlers");
    }
  }
  
  void ValidateForSkills() const {
    Validate();
    if (!skill_loader_meta) {
      throw std::runtime_error("HandlerContext: skill_loader_meta is required for skill handlers");
    }
  }
  
  void ValidateForExecApproval() const {
    Validate();
    if (!exec_approval_mgr) {
      throw std::runtime_error("HandlerContext: exec_approval_mgr is required for exec approval handlers");
    }
  }
  
  void ValidateForPlugins() const {
    Validate();
    if (!plugin_system) {
      throw std::runtime_error("HandlerContext: plugin_system is required for plugin handlers");
    }
  }
  
  void ValidateForQueue() const {
    Validate();
    if (!command_queue) {
      throw std::runtime_error("HandlerContext: command_queue is required for queue handlers");
    }
  }
};

}  // namespace gateway
}  // namespace quantclaw

#endif  // QUANTCLAW_RPCHANDLER_RPC_HANDLER_CONTEXT_HPP
