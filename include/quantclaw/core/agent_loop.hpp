// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/config.hpp"
#include <spdlog/spdlog.h>

namespace quantclaw {

class MemoryManager;
class SkillLoader;
class ToolRegistry;
class ProviderRegistry;
class SubagentManager;

// --- Agent Event (for streaming) ---

struct AgentEvent {
    std::string type;  // "text_delta" | "tool_use" | "tool_result" | "message_end"
    nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent&)>;

// --- Agent Loop ---

class AgentLoop {
public:
    AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
              std::shared_ptr<SkillLoader> skill_loader,
              std::shared_ptr<ToolRegistry> tool_registry,
              std::shared_ptr<LLMProvider> llm_provider,
              const AgentConfig& agent_config,
              std::shared_ptr<spdlog::logger> logger);

    // Process a message with externally-provided history and system prompt
    // Returns all new messages generated during the turn (assistant + tool_result)
    std::vector<Message> ProcessMessage(const std::string& message,
                                         const std::vector<Message>& history,
                                         const std::string& system_prompt);

    // Streaming version — returns all new messages generated during the turn
    std::vector<Message> ProcessMessageStream(const std::string& message,
                                                 const std::vector<Message>& history,
                                                 const std::string& system_prompt,
                                                 AgentEventCallback callback);

    // Stop the current agent turn
    void Stop();

    // Set max iterations
    void SetMaxIterations(int max) { max_iterations_ = max; }

    // Update agent config (for hot-reload)
    void SetConfig(const AgentConfig& config);

    // Get current config (for testing)
    const AgentConfig& GetConfig() const { return agent_config_; }

    // Set provider registry for dynamic model resolution
    void SetProviderRegistry(ProviderRegistry* registry) { provider_registry_ = registry; }

    // Set subagent manager for spawning child agents
    void SetSubagentManager(SubagentManager* manager) { subagent_manager_ = manager; }

    // Set model dynamically (resolves via ProviderRegistry if available)
    void SetModel(const std::string& model_ref);

private:
    // Resolve current provider (from registry or fallback to injected provider)
    std::shared_ptr<LLMProvider> resolve_provider();

    std::vector<std::string> handle_tool_calls(
        const std::vector<nlohmann::json>& tool_calls);

    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<SkillLoader> skill_loader_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::shared_ptr<LLMProvider> llm_provider_;       // Fallback / injected provider
    ProviderRegistry* provider_registry_ = nullptr;    // Non-owning, optional
    SubagentManager* subagent_manager_ = nullptr;      // Non-owning, optional
    std::shared_ptr<spdlog::logger> logger_;
    AgentConfig agent_config_;
    std::atomic<bool> stop_requested_{false};
    int max_iterations_ = 15;
};

} // namespace quantclaw
