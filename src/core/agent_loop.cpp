// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>
#include <thread>

// Bring event name constants into scope
namespace events = quantclaw::gateway::events;

namespace quantclaw {

AgentLoop::AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<SkillLoader> skill_loader,
                     std::shared_ptr<ToolRegistry> tool_registry,
                     std::shared_ptr<LLMProvider> llm_provider,
                     const AgentConfig& agent_config,
                     std::shared_ptr<spdlog::logger> logger)
    : memory_manager_(memory_manager)
    , skill_loader_(skill_loader)
    , tool_registry_(tool_registry)
    , llm_provider_(llm_provider)
    , logger_(logger)
    , agent_config_(agent_config) {
    max_iterations_ = agent_config_.max_iterations;
    logger_->info("AgentLoop initialized with model: {}", agent_config_.model);
}

std::shared_ptr<LLMProvider> AgentLoop::resolve_provider() {
    if (!provider_registry_) {
        return llm_provider_;
    }

    auto ref = provider_registry_->ResolveModel(agent_config_.model);
    auto provider = provider_registry_->GetProviderForModel(ref);
    if (provider) {
        // Update model to stripped name (without provider prefix)
        agent_config_.model = ref.model;
        return provider;
    }

    logger_->warn("Failed to resolve provider for model '{}', falling back to injected provider",
                  agent_config_.model);
    return llm_provider_;
}

void AgentLoop::SetModel(const std::string& model_ref) {
    agent_config_.model = model_ref;
    logger_->info("Model set to: {}", model_ref);
}

std::vector<Message> AgentLoop::ProcessMessage(const std::string& message,
                                                 const std::vector<Message>& history,
                                                 const std::string& system_prompt) {
    logger_->info("Processing message (non-streaming)");
    stop_requested_ = false;

    auto provider = resolve_provider();

    std::vector<Message> new_messages;

    // Build context: system + history + new user message
    std::vector<Message> context;

    // System message
    if (!system_prompt.empty()) {
        context.push_back(Message{"system", system_prompt});
    }

    // History
    for (const auto& msg : history) {
        context.push_back(msg);
    }

    // New user message
    context.push_back(Message{"user", message});

    // Create LLM request
    ChatCompletionRequest request;
    request.messages = context;
    request.model = agent_config_.model;
    request.temperature = agent_config_.temperature;
    request.max_tokens = agent_config_.max_tokens;
    request.thinking = agent_config_.thinking;

    // Add tool schemas
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& schema : tool_registry_->GetToolSchemas()) {
        nlohmann::json tool;
        tool["type"] = "function";
        tool["function"]["name"] = schema.name;
        tool["function"]["description"] = schema.description;
        tool["function"]["parameters"] = schema.parameters;
        tools_json.push_back(tool);
    }
    request.tools = tools_json.get<std::vector<nlohmann::json>>();
    request.tool_choice_auto = true;

    int iterations = 0;

    while (iterations < max_iterations_ && !stop_requested_) {
        try {
            auto response = provider->ChatCompletion(request);

            if (!response.tool_calls.empty()) {
                logger_->info("LLM requested {} tool calls", response.tool_calls.size());

                std::vector<nlohmann::json> tool_calls_json;
                for (const auto& tc : response.tool_calls) {
                    nlohmann::json tc_json;
                    tc_json["id"] = tc.id;
                    tc_json["function"]["name"] = tc.name;
                    tc_json["function"]["arguments"] = tc.arguments.dump();
                    tool_calls_json.push_back(tc_json);
                }
                auto tool_results = handle_tool_calls(tool_calls_json);

                // Assistant message: text + tool_use blocks
                Message assistant_msg;
                assistant_msg.role = "assistant";
                if (!response.content.empty())
                    assistant_msg.content.push_back(ContentBlock::MakeText(response.content));
                for (const auto& tc : response.tool_calls)
                    assistant_msg.content.push_back(ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
                request.messages.push_back(assistant_msg);
                new_messages.push_back(assistant_msg);

                // Tool results: single user message with tool_result blocks
                Message results_msg;
                results_msg.role = "user";
                for (size_t i = 0; i < response.tool_calls.size(); i++)
                    results_msg.content.push_back(
                        ContentBlock::MakeToolResult(response.tool_calls[i].id, tool_results[i]));
                request.messages.push_back(results_msg);
                new_messages.push_back(results_msg);

                iterations++;
                continue;
            }

            if (!response.content.empty()) {
                logger_->info("LLM provided final response");
                Message final_msg;
                final_msg.role = "assistant";
                final_msg.content.push_back(ContentBlock::MakeText(response.content));
                new_messages.push_back(final_msg);
                return new_messages;
            }

            logger_->error("Unexpected LLM response format");
            break;

        } catch (const std::exception& e) {
            logger_->error("Error in LLM processing: {}", e.what());
            if (iterations < max_iterations_ - 1) {
                std::this_thread::sleep_for(std::chrono::seconds(1 << iterations));
                iterations++;
                continue;
            }
            throw;
        }
    }

    if (stop_requested_) {
        Message stop_msg;
        stop_msg.role = "assistant";
        stop_msg.content.push_back(ContentBlock::MakeText("[Agent turn stopped by user]"));
        new_messages.push_back(stop_msg);
        return new_messages;
    }

    throw std::runtime_error("Failed to get valid response after " +
                             std::to_string(max_iterations_) + " iterations");
}

std::vector<Message> AgentLoop::ProcessMessageStream(const std::string& message,
                                                        const std::vector<Message>& history,
                                                        const std::string& system_prompt,
                                                        AgentEventCallback callback) {
    logger_->info("Processing message (streaming)");
    stop_requested_ = false;

    auto provider = resolve_provider();

    std::vector<Message> new_messages;

    // Build context
    std::vector<Message> context;
    if (!system_prompt.empty()) {
        context.push_back(Message{"system", system_prompt});
    }
    for (const auto& msg : history) {
        context.push_back(msg);
    }
    context.push_back(Message{"user", message});

    ChatCompletionRequest request;
    request.messages = context;
    request.model = agent_config_.model;
    request.temperature = agent_config_.temperature;
    request.max_tokens = agent_config_.max_tokens;
    request.stream = true;
    request.thinking = agent_config_.thinking;

    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& schema : tool_registry_->GetToolSchemas()) {
        nlohmann::json tool;
        tool["type"] = "function";
        tool["function"]["name"] = schema.name;
        tool["function"]["description"] = schema.description;
        tool["function"]["parameters"] = schema.parameters;
        tools_json.push_back(tool);
    }
    request.tools = tools_json.get<std::vector<nlohmann::json>>();
    request.tool_choice_auto = true;

    int iterations = 0;

    while (iterations < max_iterations_ && !stop_requested_) {
        try {
            std::string full_response;

            provider->ChatCompletionStream(request, [&](const ChatCompletionResponse& chunk) {
                if (!chunk.content.empty()) {
                    full_response += chunk.content;
                    if (callback) {
                        callback({events::kTextDelta, {{"text", chunk.content}}});
                    }
                }

                if (!chunk.tool_calls.empty()) {
                    for (const auto& tc : chunk.tool_calls) {
                        if (callback) {
                            callback({events::kToolUse, {
                                {"id", tc.id},
                                {"name", tc.name},
                                {"input", tc.arguments}
                            }});
                        }

                        // Construct assistant message with text + tool_use blocks
                        Message assistant_msg;
                        assistant_msg.role = "assistant";
                        if (!full_response.empty())
                            assistant_msg.content.push_back(ContentBlock::MakeText(full_response));
                        assistant_msg.content.push_back(ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
                        request.messages.push_back(assistant_msg);
                        new_messages.push_back(assistant_msg);
                        full_response.clear();

                        // Execute tool
                        try {
                            auto result = tool_registry_->ExecuteTool(tc.name, tc.arguments);
                            if (callback) {
                                callback({events::kToolResult, {
                                    {"tool_use_id", tc.id},
                                    {"content", result}
                                }});
                            }

                            Message results_msg;
                            results_msg.role = "user";
                            results_msg.content.push_back(
                                ContentBlock::MakeToolResult(tc.id, result));
                            request.messages.push_back(results_msg);
                            new_messages.push_back(results_msg);
                        } catch (const std::exception& e) {
                            std::string error_content = "Error: " + std::string(e.what());
                            if (callback) {
                                callback({events::kToolResult, {
                                    {"tool_use_id", tc.id},
                                    {"content", error_content},
                                    {"is_error", true}
                                }});
                            }

                            Message results_msg;
                            results_msg.role = "user";
                            results_msg.content.push_back(
                                ContentBlock::MakeToolResult(tc.id, error_content));
                            request.messages.push_back(results_msg);
                            new_messages.push_back(results_msg);
                        }
                    }
                    iterations++;
                    return; // Continue loop for tool results
                }

                if (chunk.is_stream_end) {
                    if (callback) {
                        callback({events::kMessageEnd, {
                            {"content", full_response}
                        }});
                    }
                }
            });

            // If we got a final response without tool calls, we're done
            if (!full_response.empty()) {
                Message final_msg;
                final_msg.role = "assistant";
                final_msg.content.push_back(ContentBlock::MakeText(full_response));
                new_messages.push_back(final_msg);
                return new_messages;
            }

            iterations++;

        } catch (const std::exception& e) {
            logger_->error("Error in streaming: {}", e.what());
            if (callback) {
                callback({events::kMessageEnd, {{"error", e.what()}}});
            }
            return new_messages;
        }
    }

    std::string stop_text = stop_requested_ ? "[Stopped]" : "[Max iterations reached]";
    if (callback) {
        callback({events::kMessageEnd, {{"content", stop_text}}});
    }
    Message stop_msg;
    stop_msg.role = "assistant";
    stop_msg.content.push_back(ContentBlock::MakeText(stop_text));
    new_messages.push_back(stop_msg);
    return new_messages;
}

void AgentLoop::Stop() {
    stop_requested_ = true;
    logger_->info("Agent stop requested");
}

void AgentLoop::SetConfig(const AgentConfig& config) {
    agent_config_ = config;
    max_iterations_ = config.max_iterations;
    logger_->info("AgentLoop config updated: model={}, temp={}, max_tokens={}, thinking={}",
                  config.model, config.temperature, config.max_tokens, config.thinking);
}

std::vector<std::string> AgentLoop::handle_tool_calls(const std::vector<nlohmann::json>& tool_calls) {
    std::vector<std::string> results;

    for (const auto& tool_call : tool_calls) {
        try {
            std::string tool_name = tool_call["function"]["name"];
            nlohmann::json arguments;
            const auto& args_val = tool_call["function"]["arguments"];
            if (args_val.is_string()) {
                arguments = nlohmann::json::parse(args_val.get<std::string>());
            } else {
                arguments = args_val;
            }

            logger_->info("Executing tool: {} with arguments: {}", tool_name, arguments.dump());
            std::string result = tool_registry_->ExecuteTool(tool_name, arguments);
            results.push_back(result);
            logger_->info("Tool execution successful");

        } catch (const std::exception& e) {
            logger_->error("Tool execution failed: {}", e.what());
            results.push_back("Error executing tool: " + std::string(e.what()));
        }
    }

    return results;
}

} // namespace quantclaw
