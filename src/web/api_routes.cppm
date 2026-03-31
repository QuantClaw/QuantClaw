// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.web.api_routes;

import std;
import quantclaw.config;
import quantclaw.core.agent_loop;
import quantclaw.core.prompt_builder;
import quantclaw.gateway.gateway_server;
import quantclaw.plugins.plugin_system;
import quantclaw.session.session_manager;
import quantclaw.tools.tool_registry;
import quantclaw.web.web_server;

namespace quantclaw::web {

export void register_api_routes(
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
