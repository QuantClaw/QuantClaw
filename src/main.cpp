#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "quantclaw/config.hpp"
#include "quantclaw/cli/cli_manager.hpp"
#include "quantclaw/cli/gateway_commands.hpp"
#include "quantclaw/cli/agent_commands.hpp"
#include "quantclaw/cli/session_commands.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/core/skill_loader.hpp"

static std::shared_ptr<spdlog::logger> create_logger() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("[%^%l%$] %v");
    auto logger = std::make_shared<spdlog::logger>("quantclaw", console_sink);
    spdlog::set_default_logger(logger);
    return logger;
}

int main(int argc, char* argv[]) {
    auto logger = create_logger();

    // Parse global --config / -c flag before anything else
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            quantclaw::QuantClawConfig::set_config_path(argv[i + 1]);
            // Remove --config <path> from argv so commands don't see it
            for (int j = i; j + 2 < argc; ++j) {
                argv[j] = argv[j + 2];
            }
            argc -= 2;
            break;
        }
    }

    // Create shared command handlers
    auto gateway_cmds = std::make_shared<quantclaw::cli::GatewayCommands>(logger);
    auto agent_cmds = std::make_shared<quantclaw::cli::AgentCommands>(logger);
    auto session_cmds = std::make_shared<quantclaw::cli::SessionCommands>(logger);

    // Build CLI
    quantclaw::cli::CLIManager cli;

    // --- gateway command ---
    cli.add_command({
        "gateway",
        "Manage the Gateway WebSocket server",
        {"g"},
        [gateway_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                // No subcommand: run gateway in foreground
                return gateway_cmds->foreground_command(args);
            }

            std::string sub = args[0];
            std::vector<std::string> sub_args(args.begin() + 1, args.end());

            if (sub == "install")  return gateway_cmds->install_command(sub_args);
            if (sub == "start")    return gateway_cmds->start_command(sub_args);
            if (sub == "stop")     return gateway_cmds->stop_command(sub_args);
            if (sub == "restart")  return gateway_cmds->restart_command(sub_args);
            if (sub == "status")   return gateway_cmds->status_command(sub_args);

            // Check for --port flag on direct gateway command
            if (sub == "--port" || sub == "--foreground") {
                return gateway_cmds->foreground_command(args);
            }

            std::cerr << "Unknown gateway subcommand: " << sub << std::endl;
            std::cerr << "Available: install, start, stop, restart, status" << std::endl;
            return 1;
        }
    });

    // --- agent command ---
    cli.add_command({
        "agent",
        "Send message to agent via Gateway",
        {"a"},
        [agent_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
            return agent_cmds->request_command(args);
        }
    });

    // --- sessions command ---
    cli.add_command({
        "sessions",
        "Manage sessions",
        {},
        [session_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                return session_cmds->list_command({});
            }

            std::string sub = args[0];
            std::vector<std::string> sub_args(args.begin() + 1, args.end());

            if (sub == "list")    return session_cmds->list_command(sub_args);
            if (sub == "history") return session_cmds->history_command(sub_args);
            if (sub == "delete")  return session_cmds->delete_command(sub_args);
            if (sub == "reset")   return session_cmds->reset_command(sub_args);

            std::cerr << "Unknown sessions subcommand: " << sub << std::endl;
            return 1;
        }
    });

    // --- status command (shortcut to gateway.status) ---
    cli.add_command({
        "status",
        "Show gateway status",
        {},
        [gateway_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
            return gateway_cmds->status_command(args);
        }
    });

    // --- health command ---
    cli.add_command({
        "health",
        "Gateway health check",
        {},
        [logger](int argc, char** argv) -> int {
            bool json_output = false;
            for (int i = 1; i < argc; ++i) {
                if (std::string(argv[i]) == "--json") json_output = true;
            }

            try {
                auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                    "ws://127.0.0.1:18789", "", logger);
                if (!client->connect(3000)) {
                    if (json_output) {
                        std::cout << R"({"status":"unreachable"})" << std::endl;
                    } else {
                        std::cout << "Gateway: unreachable" << std::endl;
                    }
                    return 1;
                }

                auto result = client->call("gateway.health", {});
                client->disconnect();

                if (json_output) {
                    std::cout << result.dump(2) << std::endl;
                } else {
                    std::cout << "Gateway: " << result.value("status", "unknown") << std::endl;
                    std::cout << "Version: " << result.value("version", "unknown") << std::endl;
                    std::cout << "Uptime:  " << result.value("uptime", 0) << "s" << std::endl;
                }
                return 0;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
        }
    });

    // --- config command ---
    cli.add_command({
        "config",
        "Manage configuration",
        {"c"},
        [logger](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                std::cerr << "Usage: quantclaw config <get|reload> [path]" << std::endl;
                return 1;
            }

            if (args[0] == "reload") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (!client->connect(3000)) {
                        std::cerr << "Error: Gateway not running" << std::endl;
                        return 1;
                    }
                    client->call("config.reload", {});
                    client->disconnect();
                    std::cout << "Configuration reloaded" << std::endl;
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (args[0] == "get") {
                std::string path = args.size() > 1 ? args[1] : "";
                bool json_output = false;
                for (const auto& a : args) {
                    if (a == "--json") json_output = true;
                }

                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (!client->connect(3000)) {
                        // Fallback: read config file directly
                        auto config = quantclaw::QuantClawConfig::load_from_file(
                            quantclaw::QuantClawConfig::default_config_path());
                        if (path == "gateway.port") {
                            std::cout << config.gateway.port << std::endl;
                        } else if (path == "agent.model") {
                            std::cout << config.agent.model << std::endl;
                        } else {
                            std::cerr << "Gateway not running. Limited config access." << std::endl;
                        }
                        return 0;
                    }

                    nlohmann::json params;
                    if (!path.empty()) params["path"] = path;
                    auto result = client->call("config.get", params);
                    client->disconnect();

                    if (json_output) {
                        std::cout << result.dump(2) << std::endl;
                    } else {
                        if (result.is_primitive()) {
                            std::cout << result << std::endl;
                        } else {
                            std::cout << result.dump(2) << std::endl;
                        }
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            std::cerr << "Unknown config subcommand: " << args[0] << std::endl;
            return 1;
        }
    });

    // --- skills command ---
    cli.add_command({
        "skills",
        "Manage agent skills",
        {"s"},
        [logger](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            std::string sub = args.empty() ? "list" : args[0];

            if (sub == "list") {
                // Multi-directory skill loading
                std::string home_str;
                const char* home = std::getenv("HOME");
                if (home) home_str = home;
                else home_str = "/tmp";

                auto workspace_path = std::filesystem::path(home_str) /
                                      ".quantclaw/agents/default/workspace";

                // Load config for skills settings
                quantclaw::SkillsConfig skills_config;
                try {
                    auto config = quantclaw::QuantClawConfig::load_from_file(
                        quantclaw::QuantClawConfig::default_config_path());
                    skills_config = config.skills;
                } catch (const std::exception&) {
                    // Use defaults if no config
                }

                auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger);
                auto skills = skill_loader->load_skills(skills_config, workspace_path);

                if (skills.empty()) {
                    std::cout << "No skills found" << std::endl;
                } else {
                    std::cout << "Skills (" << skills.size() << "):" << std::endl;
                    for (const auto& skill : skills) {
                        std::cout << "  ";
                        if (!skill.emoji.empty()) std::cout << skill.emoji << " ";
                        std::cout << skill.name;
                        if (!skill.description.empty()) {
                            std::cout << " - " << skill.description;
                        }
                        std::cout << std::endl;
                    }
                }
                return 0;
            }

            std::cerr << "Unknown skills subcommand: " << sub << std::endl;
            return 1;
        }
    });

    // --- doctor command ---
    cli.add_command({
        "doctor",
        "Health check (config, deps, connectivity)",
        {},
        [logger](int /*argc*/, char** /*argv*/) -> int {
            std::cout << "QuantClaw Doctor" << std::endl;
            std::cout << std::string(40, '=') << std::endl;

            // Check config file
            std::string config_path = quantclaw::QuantClawConfig::default_config_path();
            bool config_ok = std::filesystem::exists(config_path);
            std::cout << "[" << (config_ok ? "OK" : "!!") << "] Config file: "
                      << config_path << std::endl;

            // Check workspace
            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            auto workspace = std::filesystem::path(home_str) /
                             ".quantclaw/agents/default/workspace";
            bool ws_ok = std::filesystem::exists(workspace);
            std::cout << "[" << (ws_ok ? "OK" : "!!") << "] Workspace: "
                      << workspace.string() << std::endl;

            // Check SOUL.md
            auto soul_path = workspace / "SOUL.md";
            bool soul_ok = std::filesystem::exists(soul_path);
            std::cout << "[" << (soul_ok ? "OK" : "--") << "] SOUL.md" << std::endl;

            // Check gateway connectivity
            bool gw_ok = false;
            try {
                auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                    "ws://127.0.0.1:18789", "", logger);
                gw_ok = client->connect(2000);
                if (gw_ok) client->disconnect();
            } catch (...) {}
            std::cout << "[" << (gw_ok ? "OK" : "!!") << "] Gateway: "
                      << (gw_ok ? "running" : "not running") << std::endl;

            std::cout << std::string(40, '=') << std::endl;
            return (config_ok && ws_ok) ? 0 : 1;
        }
    });

    return cli.run(argc, argv);
}
