// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <functional>
#include <spdlog/spdlog.h>

namespace quantclaw {
    class SessionManager;
    class AgentLoop;
    class PromptBuilder;
    class ToolRegistry;
    struct QuantClawConfig;
}

namespace quantclaw::gateway {
    class GatewayServer;
}

namespace quantclaw::web {

class WebServer;

void register_api_routes(
    WebServer& server,
    std::shared_ptr<quantclaw::SessionManager> session_manager,
    std::shared_ptr<quantclaw::AgentLoop> agent_loop,
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
    const quantclaw::QuantClawConfig& config,
    quantclaw::gateway::GatewayServer& gateway_server,
    std::shared_ptr<spdlog::logger> logger,
    std::function<void()> reload_fn = nullptr);

} // namespace quantclaw::web
