// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include "quantclaw/common/noncopyable.hpp"
#include "quantclaw/config.hpp"

export module quantclaw.core.agent_loop;

import std;
import nlohmann.json;

namespace spdlog {
class logger;
}

import quantclaw.core.dag_runtime;
export namespace quantclaw {

class MemoryManager;
class SkillLoader;
class ToolRegistry;
class ProviderRegistry;
class SubagentManager;
class FailoverResolver;
class DagRuntime;
class ContextEngine;
class UsageAccumulator;
class LLMProvider;
struct Message;

struct AgentEvent {
  std::string type;
  nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent&)>;

class AgentLoop : public Noncopyable {
 public:
  AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
            std::shared_ptr<SkillLoader> skill_loader,
            std::shared_ptr<ToolRegistry> tool_registry,
            std::shared_ptr<LLMProvider> llm_provider,
            const AgentConfig& agent_config,
            std::shared_ptr<spdlog::logger> logger);

  std::vector<Message>
  ProcessMessage(const std::string& message,
                 const std::vector<Message>& history,
                 const std::string& system_prompt,
                 const std::string& usage_session_key = "");

  std::vector<Message> ProcessMessageStream(
      const std::string& message, const std::vector<Message>& history,
      const std::string& system_prompt, AgentEventCallback callback,
      const std::string& usage_session_key = "");

  void Stop();

  void SetMaxIterations(int max) {
    max_iterations_ = max;
  }

  void SetConfig(const AgentConfig& config);

  const AgentConfig& GetConfig() const {
    return agent_config_;
  }

  void SetProviderRegistry(ProviderRegistry* registry) {
    provider_registry_ = registry;
  }

  void SetSubagentManager(SubagentManager* manager) {
    subagent_manager_ = manager;
  }

  void SetFailoverResolver(FailoverResolver* resolver) {
    failover_resolver_ = resolver;
  }

  void SetContextEngine(std::shared_ptr<ContextEngine> engine) {
    context_engine_ = std::move(engine);
  }

  void SetSessionKey(const std::string& key) {
    session_key_ = key;
  }

  void SetUsageAccumulator(std::shared_ptr<UsageAccumulator> acc) {
    usage_accumulator_ = acc;
  }

  std::shared_ptr<UsageAccumulator> GetUsageAccumulator() const {
    return usage_accumulator_;
  }

  void SetModel(const std::string& model_ref);

  void SetDagRuntime(std::shared_ptr<DagRuntime> dag_runtime) {
    dag_runtime_ = std::move(dag_runtime);
  }

  std::string GetLatestDagRunIdForSession(const std::string& session_key) const {
    if (!dag_runtime_) {
      return "";
    }
    return dag_runtime_->LatestRunIdForSession(session_key);
  }

 private:
  std::shared_ptr<LLMProvider> resolve_provider();

  std::vector<std::string>
  handle_tool_calls(const std::vector<nlohmann::json>& tool_calls);

  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::shared_ptr<LLMProvider> llm_provider_;
  ProviderRegistry* provider_registry_ = nullptr;
  SubagentManager* subagent_manager_ = nullptr;
  FailoverResolver* failover_resolver_ = nullptr;
  std::shared_ptr<UsageAccumulator> usage_accumulator_;
  std::shared_ptr<DagRuntime> dag_runtime_;
  std::shared_ptr<ContextEngine> context_engine_;
  std::string session_key_;
  std::shared_ptr<spdlog::logger> logger_;
  AgentConfig agent_config_;
  std::atomic<bool> stop_requested_{false};
  int max_iterations_ = 15;

  std::string last_provider_id_;
  std::string last_profile_id_;
};

}  // namespace quantclaw
