// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>

#include <spdlog/spdlog.h>

namespace quantclaw {
class SessionManager;
class AgentLoop;
class PromptBuilder;
class ToolRegistry;
class PluginSystem;
struct QuantClawConfig;
}  // namespace quantclaw

namespace quantclaw::gateway {
class GatewayServer;
}

namespace quantclaw::web {

class WebServer;

void register_api_routes(
    WebServer& server,
    const std::shared_ptr<quantclaw::SessionManager>& session_manager,
    const std::shared_ptr<quantclaw::AgentLoop>& agent_loop,
    const std::shared_ptr<quantclaw::PromptBuilder>& prompt_builder,
    const std::shared_ptr<quantclaw::ToolRegistry>& tool_registry,
    const quantclaw::QuantClawConfig& config,
    quantclaw::gateway::GatewayServer& gateway_server,
    const std::shared_ptr<spdlog::logger>& logger,
    const std::function<void()>& reload_fn = nullptr,
    quantclaw::PluginSystem* plugin_system = nullptr);

}  // namespace quantclaw::web
