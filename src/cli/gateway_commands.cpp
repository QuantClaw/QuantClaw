// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/cli/gateway_commands.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/daemon_manager.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/config.hpp"
#include "quantclaw/core/signal_handler.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/security/tool_permissions.hpp"
#include "quantclaw/mcp/mcp_tool_manager.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/channels/adapter_manager.hpp"
#include "quantclaw/web/web_server.hpp"
#include "quantclaw/web/api_routes.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/core/subagent.hpp"
#include "quantclaw/security/exec_approval.hpp"
#include "quantclaw/security/rbac.hpp"
#include "quantclaw/security/rate_limiter.hpp"
#include "quantclaw/platform/process.hpp"
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>

// Forward declare from rpc_handlers.cpp
namespace quantclaw::gateway {
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
        std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr = nullptr);
}

namespace quantclaw::cli {

GatewayCommands::GatewayCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
}

int GatewayCommands::ForegroundCommand(const std::vector<std::string>& args) {
    // Load configuration first (CLI flags override later)
    quantclaw::QuantClawConfig config;
    try {
        config = quantclaw::QuantClawConfig::LoadFromFile(
            quantclaw::QuantClawConfig::DefaultConfigPath());
    } catch (const std::exception& e) {
        logger_->warn("No config file found, using defaults: {}", e.what());
    }

    // Apply CLI flag overrides
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) {
            config.gateway.port = std::stoi(args[++i]);
        } else if (args[i] == "--bind" && i + 1 < args.size()) {
            config.gateway.bind = args[++i];
        } else if (args[i] == "--auth" && i + 1 < args.size()) {
            config.gateway.auth.mode = args[++i];
        } else if (args[i] == "--token" && i + 1 < args.size()) {
            config.gateway.auth.token = args[++i];
        } else if (args[i] == "--verbose") {
            logger_->set_level(spdlog::level::debug);
        }
    }

    int port = config.gateway.port;
    logger_->info("Starting Gateway in foreground mode on port {}", port);

    std::string home_str = platform::home_directory();

    std::filesystem::path base_dir = std::filesystem::path(home_str) / ".quantclaw";
    std::filesystem::path workspace_dir = base_dir / "agents" / "main" / "workspace";
    std::filesystem::path sessions_dir = base_dir / "agents" / "main" / "sessions";

    std::filesystem::create_directories(workspace_dir);
    std::filesystem::create_directories(sessions_dir);

    // Initialize components
    auto memory_manager = std::make_shared<quantclaw::MemoryManager>(workspace_dir, logger_);
    memory_manager->LoadWorkspaceFiles();

    auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger_);
    auto tool_registry = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry->RegisterBuiltinTools();
    tool_registry->RegisterChainTool();

    // Discover and register MCP tools
    auto mcp_tool_manager = std::make_shared<quantclaw::mcp::MCPToolManager>(logger_);
    if (!config.mcp.servers.empty()) {
        mcp_tool_manager->DiscoverTools(config.mcp);
        mcp_tool_manager->RegisterInto(*tool_registry);
    }

    // Set up tool permissions
    auto permission_checker = std::make_shared<quantclaw::ToolPermissionChecker>(config.tools_permission);
    tool_registry->SetPermissionChecker(permission_checker);
    tool_registry->SetMcpToolManager(mcp_tool_manager);

    // Initialize provider registry
    auto provider_registry = std::make_shared<quantclaw::ProviderRegistry>(logger_);
    provider_registry->RegisterBuiltinFactories();

    // Load provider entries from config (apiKey, baseUrl, timeout)
    for (const auto& [id, prov] : config.providers) {
        quantclaw::ProviderEntry entry;
        entry.id = id;
        entry.api_key = prov.api_key;
        entry.base_url = prov.base_url;
        entry.timeout = prov.timeout;
        provider_registry->AddProvider(entry);
    }

    // Resolve the configured model to get initial provider
    auto model_ref = provider_registry->ResolveModel(config.agent.model);
    config.agent.model = model_ref.model;  // strip prefix before passing to API
    auto llm_provider = provider_registry->GetProviderForModel(model_ref);
    if (!llm_provider) {
        logger_->error("Failed to resolve provider for model: {}", config.agent.model);
        return 1;
    }

    auto agent_loop = std::make_shared<quantclaw::AgentLoop>(
        memory_manager, skill_loader, tool_registry, llm_provider, config.agent, logger_);
    agent_loop->SetProviderRegistry(provider_registry.get());

    auto session_manager = std::make_shared<quantclaw::SessionManager>(sessions_dir, logger_);

    auto prompt_builder = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager, skill_loader, tool_registry, &config);

    // Create and configure gateway server
    gateway::GatewayServer server(port, logger_);

    // Tell WS server to redirect plain HTTP requests to the Control UI port
    if (config.gateway.control_ui.enabled) {
        server.SetHttpRedirectPort(config.gateway.control_ui.port);
    }

    // Configure auth: prefer env var, fall back to config file
    std::string auth_token = config.gateway.auth.token;
    const char* env_token = std::getenv("QUANTCLAW_AUTH_TOKEN");
    if (env_token && strlen(env_token) > 0) {
        auth_token = env_token;
    }
    server.SetAuth(config.gateway.auth.mode, auth_token);

    // Enable RBAC
    auto rbac_checker = std::make_shared<quantclaw::RBACChecker>();
    server.SetRbac(rbac_checker);

    // Enable rate limiting
    quantclaw::RateLimiter::Config rl_config;
    if (config.security.permission_level == "strict") {
        rl_config.max_requests = 60;
        rl_config.window_seconds = 60;
        rl_config.burst_max = 10;
    }
    auto rate_limiter = std::make_shared<quantclaw::RateLimiter>(rl_config);
    server.SetRateLimiter(rate_limiter);

    // Start file watcher
    memory_manager->StartFileWatcher();

    // Build reusable reload function
    std::string config_path = quantclaw::QuantClawConfig::DefaultConfigPath();
    std::function<void()> reload_fn = [&config, agent_loop, tool_registry, mcp_tool_manager, memory_manager, this]() {
        logger_->info("Reload signal received");
        try {
            config = quantclaw::QuantClawConfig::LoadFromFile(
                quantclaw::QuantClawConfig::DefaultConfigPath());

            // Propagate to AgentLoop
            agent_loop->SetConfig(config.agent);

            // Rebuild permissions
            auto new_checker = std::make_shared<quantclaw::ToolPermissionChecker>(config.tools_permission);
            tool_registry->SetPermissionChecker(new_checker);

            // Re-discover MCP tools if server list changed
            if (!config.mcp.servers.empty()) {
                mcp_tool_manager->DiscoverTools(config.mcp);
                mcp_tool_manager->RegisterInto(*tool_registry);
            }

            // Reload workspace files
            memory_manager->LoadWorkspaceFiles();

            logger_->info("Configuration reloaded and propagated");
        } catch (const std::exception& e) {
            logger_->error("Failed to reload config: {}", e.what());
        }
    };

    // Initialize cron scheduler
    auto cron_scheduler = std::make_shared<quantclaw::CronScheduler>(logger_);
    std::string cron_file = (base_dir / "cron.json").string();
    if (std::filesystem::exists(cron_file)) {
        cron_scheduler->Load(cron_file);
    }

    // Initialize exec approval manager
    auto exec_approval_mgr = std::make_shared<quantclaw::ExecApprovalManager>(logger_);
    if (!config.exec_approval_config.is_null()) {
        auto approval_cfg = quantclaw::ExecApprovalConfig::FromJson(config.exec_approval_config);
        exec_approval_mgr->Configure(approval_cfg);
    }

    // Connect approval manager to tool registry
    tool_registry->SetApprovalManager(exec_approval_mgr);

    // Initialize subagent manager
    auto subagent_manager = std::make_shared<quantclaw::SubagentManager>(logger_);
    if (!config.subagent_config.is_null()) {
        auto sub_cfg = quantclaw::SubagentConfig::FromJson(config.subagent_config);
        subagent_manager->Configure(sub_cfg);
    }

    // Wire subagent runner to agent loop (synchronous for now)
    subagent_manager->SetAgentRunner(
        [agent_loop, session_manager, prompt_builder](
            const std::string& session_key,
            const std::string& task,
            const std::string& /*model*/,
            const std::string& extra_system_prompt) -> std::string {
            // Get or create child session
            session_manager->GetOrCreate(session_key, "subagent");

            // Build system prompt
            std::string system_prompt = prompt_builder->BuildFull();
            if (!extra_system_prompt.empty()) {
                system_prompt += "\n\n" + extra_system_prompt;
            }

            // Run agent loop on child session
            auto messages = agent_loop->ProcessMessage(task, {}, system_prompt);

            // Extract final response
            std::string result;
            for (const auto& msg : messages) {
                if (msg.role == "assistant") {
                    result = msg.text();
                }
            }
            return result;
        }
    );

    // Connect subagent manager to tool registry and agent loop
    tool_registry->SetSubagentManager(subagent_manager.get(), "main");
    agent_loop->SetSubagentManager(subagent_manager.get());

    // Register RPC handlers
    gateway::register_rpc_handlers(server, session_manager, agent_loop, prompt_builder,
        tool_registry, config, logger_, reload_fn, provider_registry,
        skill_loader, cron_scheduler, exec_approval_mgr);

    // Start server
    try {
        server.Start();
    } catch (const std::exception& e) {
        logger_->error("Failed to start gateway: {}", e.what());
        return 1;
    }

    logger_->info("Gateway running on ws://0.0.0.0:{}", port);

    // Start HTTP API server (Control UI)
    std::unique_ptr<quantclaw::web::WebServer> http_server;
    if (config.gateway.control_ui.enabled) {
        int http_port = config.gateway.control_ui.port;
        http_server = std::make_unique<quantclaw::web::WebServer>(http_port, logger_);
        http_server->EnableCors("*");

        if (!auth_token.empty() && config.gateway.auth.mode == "token") {
            http_server->SetAuthToken(auth_token);
        }

        quantclaw::web::register_api_routes(
            *http_server, session_manager, agent_loop, prompt_builder,
            tool_registry, config, server, logger_, reload_fn);

        // Mount dashboard UI if available
        // Search order: 1) ~/.quantclaw/ui/  2) <exe_dir>/dashboard/dist/  3) <exe_dir>/../dashboard/dist/
        std::string ui_dir;
        std::string candidate1 = (base_dir / "ui").string();
        if (std::filesystem::exists(candidate1)) {
            ui_dir = candidate1;
        } else {
            auto exe_dir = std::filesystem::path(platform::executable_path()).parent_path();
            std::string candidate2 = (exe_dir / "dashboard" / "dist").string();
            std::string candidate3 = (exe_dir.parent_path() / "dashboard" / "dist").string();
            if (std::filesystem::exists(candidate2)) {
                ui_dir = candidate2;
            } else if (std::filesystem::exists(candidate3)) {
                ui_dir = candidate3;
            }
        }
        if (!ui_dir.empty()) {
            http_server->SetMountPoint("/__quantclaw__/control/", ui_dir);
            logger_->info("Dashboard UI mounted from {}", ui_dir);

            // Redirect / to control UI
            http_server->AddRawRoute("/", "GET",
                [](const httplib::Request&, httplib::Response& res) {
                    res.set_redirect("/__quantclaw__/control/");
                }
            );
        }

        // Gateway info endpoint for UI to discover WebSocket port
        http_server->AddRawRoute("/api/gateway-info", "GET",
            [port](const httplib::Request&, httplib::Response& res) {
                nlohmann::json info = {
                    {"wsUrl", "ws://localhost:" + std::to_string(port)},
                    {"wsPort", port},
                    {"version", "0.2.0"}
                };
                res.status = 200;
                res.set_content(info.dump(), "application/json");
            }
        );

        http_server->Start();
        logger_->info("HTTP API running on http://0.0.0.0:{}", http_port);
    }

    // Start channel adapters (Discord, Telegram, etc.)
    std::unique_ptr<quantclaw::ChannelAdapterManager> adapter_manager;
    if (!config.channels.empty()) {
        adapter_manager = std::make_unique<quantclaw::ChannelAdapterManager>(
            port, auth_token, config.channels, logger_);
        adapter_manager->Start();
    }

    logger_->info("Press Ctrl+C to stop");

    // Install signal handler
    quantclaw::SignalHandler::Install([&server, &http_server, &adapter_manager, this]() {
        logger_->info("Shutdown signal received");
        if (adapter_manager) adapter_manager->Stop();
        if (http_server) http_server->Stop();
        server.Stop();
    }, reload_fn);

    // Start config file watcher thread
    std::atomic<bool> watching{true};
    std::filesystem::file_time_type config_mtime;
    try {
        config_mtime = std::filesystem::last_write_time(config_path);
    } catch (const std::exception&) {
        // Config file may not exist yet
    }

    std::thread config_watcher([&config_path, &config_mtime, &reload_fn, &watching, logger = logger_]() {
        while (watching.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!watching.load()) break;
            try {
                auto current_mtime = std::filesystem::last_write_time(config_path);
                if (current_mtime != config_mtime) {
                    logger->info("Config file changed, reloading...");
                    reload_fn();
                    config_mtime = current_mtime;
                }
            } catch (const std::exception&) {
                // File may have been deleted or is temporarily unavailable
            }
        }
    });

    // Block until shutdown
    quantclaw::SignalHandler::WaitForShutdown();

    // Stop config watcher
    watching.store(false);
    if (config_watcher.joinable()) {
        config_watcher.join();
    }

    if (adapter_manager) adapter_manager->Stop();
    if (http_server) http_server->Stop();
    server.Stop();
    logger_->info("Gateway stopped gracefully");
    return 0;
}

int GatewayCommands::InstallCommand(const std::vector<std::string>& args) {
    int port = 18789;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) {
            port = std::stoi(args[++i]);
        }
    }

    gateway::DaemonManager daemon(logger_);
    return daemon.Install(port);
}

int GatewayCommands::UninstallCommand(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.Uninstall();
}

int GatewayCommands::CallCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: quantclaw gateway call <method> [json-params]" << std::endl;
        return 1;
    }

    std::string method = args[0];
    nlohmann::json params = nlohmann::json::object();
    if (args.size() > 1) {
        try {
            params = nlohmann::json::parse(args[1]);
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Invalid JSON params: " << e.what() << std::endl;
            return 1;
        }
    }

    try {
        auto client = std::make_shared<gateway::GatewayClient>(gateway_url_, "", logger_);
        if (!client->Connect(3000)) {
            std::cerr << "Error: Gateway not running" << std::endl;
            return 1;
        }

        auto result = client->Call(method, params);
        client->Disconnect();
        std::cout << result.dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int GatewayCommands::StartCommand(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.Start();
}

int GatewayCommands::StopCommand(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.Stop();
}

int GatewayCommands::RestartCommand(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.Restart();
}

int GatewayCommands::StatusCommand(const std::vector<std::string>& args) {
    bool json_output = false;
    for (const auto& arg : args) {
        if (arg == "--json") json_output = true;
    }

    // First try connecting to the gateway via RPC
    try {
        auto client = std::make_shared<gateway::GatewayClient>(gateway_url_, "", logger_);
        if (client->Connect(3000)) {
            auto result = client->Call("gateway.status", {});
            client->Disconnect();

            if (json_output) {
                std::cout << result.dump(2) << std::endl;
            } else {
                std::cout << "Gateway Status:" << std::endl;
                std::cout << "  Running:     " << (result.value("running", false) ? "yes" : "no") << std::endl;
                std::cout << "  Port:        " << result.value("port", 0) << std::endl;
                std::cout << "  Connections: " << result.value("connections", 0) << std::endl;
                std::cout << "  Sessions:    " << result.value("sessions", 0) << std::endl;
                std::cout << "  Uptime:      " << result.value("uptime", 0) << "s" << std::endl;
                std::cout << "  Version:     " << result.value("version", "unknown") << std::endl;
            }
            return 0;
        }
    } catch (const std::exception&) {}

    // Fallback: check daemon status
    gateway::DaemonManager daemon(logger_);
    if (daemon.IsRunning()) {
        std::cout << "Gateway daemon is running (PID: " << daemon.GetPid() << ")" << std::endl;
        std::cout << "But could not connect via WebSocket" << std::endl;
    } else {
        std::cout << "Gateway is not running" << std::endl;
    }
    return 1;
}

} // namespace quantclaw::cli
