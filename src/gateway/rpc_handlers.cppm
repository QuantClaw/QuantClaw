// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.gateway.rpc_handlers;

import std;
import nlohmann.json;

import quantclaw.config;
import quantclaw.core.agent_loop;
import quantclaw.core.cron_scheduler;
import quantclaw.core.memory_search;
import quantclaw.core.prompt_builder;
import quantclaw.core.session_compaction;
import quantclaw.core.skill_loader;
import quantclaw.gateway.command_queue;
import quantclaw.gateway.gateway_server;
import quantclaw.gateway.protocol;
import quantclaw.plugins.plugin_system;
import quantclaw.providers.provider_registry;
import quantclaw.security.exec_approval;
import quantclaw.session.session_manager;
import quantclaw.tools.tool_chain;
import quantclaw.tools.tool_registry;

export namespace quantclaw::gateway {

void register_rpc_handlers(
    GatewayServer& server,
    std::shared_ptr<quantclaw::SessionManager> session_manager,
    std::shared_ptr<quantclaw::AgentLoop> agent_loop,
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
    const quantclaw::QuantClawConfig& config,
    std::shared_ptr<spdlog::logger> logger,
    std::function<void()> reload_fn = nullptr,
    std::shared_ptr<quantclaw::ProviderRegistry> provider_registry = nullptr,
    std::shared_ptr<quantclaw::SkillLoader> skill_loader = nullptr,
    std::shared_ptr<quantclaw::CronScheduler> cron_scheduler = nullptr,
    std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr = nullptr,
    quantclaw::PluginSystem* plugin_system = nullptr,
    CommandQueue* command_queue = nullptr,
    std::string log_file_path = {});

}  // namespace quantclaw::gateway
