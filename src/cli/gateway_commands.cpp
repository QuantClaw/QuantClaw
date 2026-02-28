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
#include "quantclaw/providers/openai_provider.hpp"
#include "quantclaw/providers/anthropic_provider.hpp"
#include "quantclaw/channels/adapter_manager.hpp"
#include "quantclaw/web/web_server.hpp"
#include "quantclaw/web/api_routes.hpp"
#include "quantclaw/core/skill_loader.hpp"
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
        std::function<void()> reload_fn = nullptr);
}

namespace quantclaw::cli {

GatewayCommands::GatewayCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
}

int GatewayCommands::foreground_command(const std::vector<std::string>& args) {
    // Load configuration first (CLI flags override later)
    quantclaw::QuantClawConfig config;
    try {
        config = quantclaw::QuantClawConfig::load_from_file(
            quantclaw::QuantClawConfig::default_config_path());
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

    // Expand home directory
    std::string home_str;
    const char* home = std::getenv("HOME");
    if (home) home_str = home;
    else home_str = "/tmp";

    std::filesystem::path base_dir = std::filesystem::path(home_str) / ".quantclaw";
    std::filesystem::path workspace_dir = base_dir / "agents" / "main" / "workspace";
    std::filesystem::path sessions_dir = base_dir / "agents" / "main" / "sessions";

    std::filesystem::create_directories(workspace_dir);
    std::filesystem::create_directories(sessions_dir);

    // Initialize components
    auto memory_manager = std::make_shared<quantclaw::MemoryManager>(workspace_dir, logger_);
    memory_manager->load_workspace_files();

    auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger_);
    auto tool_registry = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry->register_builtin_tools();
    tool_registry->register_chain_tool();

    // Discover and register MCP tools
    auto mcp_tool_manager = std::make_shared<quantclaw::mcp::MCPToolManager>(logger_);
    if (!config.mcp.servers.empty()) {
        mcp_tool_manager->discover_tools(config.mcp);
        mcp_tool_manager->register_into(*tool_registry);
    }

    // Set up tool permissions
    auto permission_checker = std::make_shared<quantclaw::ToolPermissionChecker>(config.tools_permission);
    tool_registry->set_permission_checker(permission_checker);
    tool_registry->set_mcp_tool_manager(mcp_tool_manager);

    // Initialize LLM provider via model prefix routing
    // Model format: "provider/model-name" (e.g. "openai/qwen-max")
    // If no prefix, defaults to "openai"
    std::string model_str = config.agent.model;
    std::string provider_prefix = "openai";
    std::string model_name = model_str;

    auto slash_pos = model_str.find('/');
    if (slash_pos != std::string::npos) {
        provider_prefix = model_str.substr(0, slash_pos);
        model_name = model_str.substr(slash_pos + 1);
    }
    config.agent.model = model_name;  // strip prefix before passing to API

    std::shared_ptr<quantclaw::LLMProvider> llm_provider;
    if (provider_prefix == "anthropic") {
        std::string api_key;
        std::string base_url = "https://api.anthropic.com";
        int timeout = 30;
        if (config.providers.count("anthropic")) {
            api_key = config.providers["anthropic"].api_key;
            if (!config.providers["anthropic"].base_url.empty()) {
                base_url = config.providers["anthropic"].base_url;
            }
            timeout = config.providers["anthropic"].timeout;
        }
        llm_provider = std::make_shared<quantclaw::AnthropicProvider>(
            api_key, base_url, timeout, logger_);
    } else {
        std::string api_key;
        std::string base_url = "https://api.openai.com/v1";
        int timeout = 30;
        if (config.providers.count("openai")) {
            api_key = config.providers["openai"].api_key;
            if (!config.providers["openai"].base_url.empty()) {
                base_url = config.providers["openai"].base_url;
            }
            timeout = config.providers["openai"].timeout;
        }
        llm_provider = std::make_shared<quantclaw::OpenAIProvider>(
            api_key, base_url, timeout, logger_);
    }

    auto agent_loop = std::make_shared<quantclaw::AgentLoop>(
        memory_manager, skill_loader, tool_registry, llm_provider, config.agent, logger_);

    auto session_manager = std::make_shared<quantclaw::SessionManager>(sessions_dir, logger_);

    auto prompt_builder = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager, skill_loader, tool_registry, &config);

    // Create and configure gateway server
    gateway::GatewayServer server(port, logger_);

    // Tell WS server to redirect plain HTTP requests to the Control UI port
    if (config.gateway.control_ui.enabled) {
        server.set_http_redirect_port(config.gateway.control_ui.port);
    }

    // Configure auth: prefer env var, fall back to config file
    std::string auth_token = config.gateway.auth.token;
    const char* env_token = std::getenv("QUANTCLAW_AUTH_TOKEN");
    if (env_token && strlen(env_token) > 0) {
        auth_token = env_token;
    }
    server.set_auth(config.gateway.auth.mode, auth_token);

    // Start file watcher
    memory_manager->start_file_watcher();

    // Build reusable reload function
    std::string config_path = quantclaw::QuantClawConfig::default_config_path();
    std::function<void()> reload_fn = [&config, agent_loop, tool_registry, mcp_tool_manager, memory_manager, this]() {
        logger_->info("Reload signal received");
        try {
            config = quantclaw::QuantClawConfig::load_from_file(
                quantclaw::QuantClawConfig::default_config_path());

            // Propagate to AgentLoop
            agent_loop->set_config(config.agent);

            // Rebuild permissions
            auto new_checker = std::make_shared<quantclaw::ToolPermissionChecker>(config.tools_permission);
            tool_registry->set_permission_checker(new_checker);

            // Re-discover MCP tools if server list changed
            if (!config.mcp.servers.empty()) {
                mcp_tool_manager->discover_tools(config.mcp);
                mcp_tool_manager->register_into(*tool_registry);
            }

            // Reload workspace files
            memory_manager->load_workspace_files();

            logger_->info("Configuration reloaded and propagated");
        } catch (const std::exception& e) {
            logger_->error("Failed to reload config: {}", e.what());
        }
    };

    // Register RPC handlers
    gateway::register_rpc_handlers(server, session_manager, agent_loop, prompt_builder, tool_registry, config, logger_, reload_fn);

    // Start server
    try {
        server.start();
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
        http_server->enable_cors("*");

        if (!auth_token.empty() && config.gateway.auth.mode == "token") {
            http_server->set_auth_token(auth_token);
        }

        quantclaw::web::register_api_routes(
            *http_server, session_manager, agent_loop, prompt_builder,
            tool_registry, config, server, logger_, reload_fn);

        // Mount dashboard UI if available
        std::string ui_dir = (base_dir / "ui").string();
        if (std::filesystem::exists(ui_dir)) {
            http_server->set_mount_point("/", ui_dir);
            logger_->info("Dashboard UI mounted from {}", ui_dir);
        }

        // Gateway info endpoint for UI to discover WebSocket port
        http_server->add_raw_route("/api/gateway-info", "GET",
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

        http_server->start();
        logger_->info("HTTP API running on http://0.0.0.0:{}", http_port);
    }

    // Start channel adapters (Discord, Telegram, etc.)
    std::unique_ptr<quantclaw::ChannelAdapterManager> adapter_manager;
    if (!config.channels.empty()) {
        adapter_manager = std::make_unique<quantclaw::ChannelAdapterManager>(
            port, auth_token, config.channels, logger_);
        adapter_manager->start();
    }

    logger_->info("Press Ctrl+C to stop");

    // Install signal handler
    quantclaw::SignalHandler::install([&server, &http_server, &adapter_manager, this]() {
        logger_->info("Shutdown signal received");
        if (adapter_manager) adapter_manager->stop();
        if (http_server) http_server->stop();
        server.stop();
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
    quantclaw::SignalHandler::wait_for_shutdown();

    // Stop config watcher
    watching.store(false);
    if (config_watcher.joinable()) {
        config_watcher.join();
    }

    if (adapter_manager) adapter_manager->stop();
    if (http_server) http_server->stop();
    server.stop();
    logger_->info("Gateway stopped gracefully");
    return 0;
}

int GatewayCommands::install_command(const std::vector<std::string>& args) {
    int port = 18789;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) {
            port = std::stoi(args[++i]);
        }
    }

    gateway::DaemonManager daemon(logger_);
    return daemon.install(port);
}

int GatewayCommands::uninstall_command(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.uninstall();
}

int GatewayCommands::call_command(const std::vector<std::string>& args) {
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
        if (!client->connect(3000)) {
            std::cerr << "Error: Gateway not running" << std::endl;
            return 1;
        }

        auto result = client->call(method, params);
        client->disconnect();
        std::cout << result.dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int GatewayCommands::start_command(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.start();
}

int GatewayCommands::stop_command(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.stop();
}

int GatewayCommands::restart_command(const std::vector<std::string>& /*args*/) {
    gateway::DaemonManager daemon(logger_);
    return daemon.restart();
}

int GatewayCommands::status_command(const std::vector<std::string>& args) {
    bool json_output = false;
    for (const auto& arg : args) {
        if (arg == "--json") json_output = true;
    }

    // First try connecting to the gateway via RPC
    try {
        auto client = std::make_shared<gateway::GatewayClient>(gateway_url_, "", logger_);
        if (client->connect(3000)) {
            auto result = client->call("gateway.status", {});
            client->disconnect();

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
    if (daemon.is_running()) {
        std::cout << "Gateway daemon is running (PID: " << daemon.get_pid() << ")" << std::endl;
        std::cout << "But could not connect via WebSocket" << std::endl;
    } else {
        std::cout << "Gateway is not running" << std::endl;
    }
    return 1;
}

} // namespace quantclaw::cli
