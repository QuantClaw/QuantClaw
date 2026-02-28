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
#include "quantclaw/core/memory_search.hpp"

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

            if (sub == "run")       return gateway_cmds->foreground_command(sub_args);
            if (sub == "install")   return gateway_cmds->install_command(sub_args);
            if (sub == "uninstall") return gateway_cmds->uninstall_command(sub_args);
            if (sub == "start")     return gateway_cmds->start_command(sub_args);
            if (sub == "stop")      return gateway_cmds->stop_command(sub_args);
            if (sub == "restart")   return gateway_cmds->restart_command(sub_args);
            if (sub == "status")    return gateway_cmds->status_command(sub_args);
            if (sub == "call")      return gateway_cmds->call_command(sub_args);

            // Flags on direct gateway command → foreground mode
            if (sub == "--port" || sub == "--foreground" || sub == "--bind") {
                return gateway_cmds->foreground_command(args);
            }

            std::cerr << "Unknown gateway subcommand: " << sub << std::endl;
            std::cerr << "Available: run, install, uninstall, start, stop, "
                         "restart, status, call" << std::endl;
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

            // Check for "agent stop" subcommand
            if (!args.empty() && args[0] == "stop") {
                std::vector<std::string> sub_args(args.begin() + 1, args.end());
                return agent_cmds->stop_command(sub_args);
            }

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
            int timeout_ms = 3000;

            for (int i = 1; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--json") {
                    json_output = true;
                } else if (arg == "--timeout" && i + 1 < argc) {
                    timeout_ms = std::stoi(argv[++i]);
                }
            }

            try {
                auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                    "ws://127.0.0.1:18789", "", logger);
                if (!client->connect(timeout_ms)) {
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
                std::cerr << "Usage: quantclaw config <get|set|unset|reload> [path] [value]"
                          << std::endl;
                return 1;
            }

            std::string sub = args[0];

            if (sub == "reload") {
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

            if (sub == "get") {
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
                            std::cerr << "Gateway not running. Limited config access."
                                      << std::endl;
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

            if (sub == "set") {
                if (args.size() < 3) {
                    std::cerr << "Usage: quantclaw config set <path> <value>"
                              << std::endl;
                    return 1;
                }
                std::string path = args[1];
                std::string raw_value = args[2];

                // Parse value: try JSON first, then treat as string
                nlohmann::json value;
                try {
                    value = nlohmann::json::parse(raw_value);
                } catch (const nlohmann::json::exception&) {
                    value = raw_value;
                }

                try {
                    auto config_file = quantclaw::QuantClawConfig::default_config_path();
                    quantclaw::QuantClawConfig::set_value(config_file, path, value);
                    std::cout << path << " = " << value.dump() << std::endl;

                    // Notify running gateway to reload
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(1000)) {
                        client->call("config.reload", {});
                        client->disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "unset") {
                if (args.size() < 2) {
                    std::cerr << "Usage: quantclaw config unset <path>" << std::endl;
                    return 1;
                }
                std::string path = args[1];

                try {
                    auto config_file = quantclaw::QuantClawConfig::default_config_path();
                    quantclaw::QuantClawConfig::unset_value(config_file, path);
                    std::cout << "Removed: " << path << std::endl;

                    // Notify running gateway to reload
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(1000)) {
                        client->call("config.reload", {});
                        client->disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            std::cerr << "Unknown config subcommand: " << sub << std::endl;
            std::cerr << "Available: get, set, unset, reload" << std::endl;
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
                                      ".quantclaw/agents/main/workspace";

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
                             ".quantclaw/agents/main/workspace";
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

    // --- cron command ---
    cli.add_command({
        "cron",
        "Manage scheduled tasks",
        {},
        [logger](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            std::string cron_file = home_str + "/.quantclaw/cron.json";

            if (args.empty() || args[0] == "list") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(3000)) {
                        auto result = client->call("cron.list", {});
                        client->disconnect();
                        if (result.is_array()) {
                            if (result.empty()) {
                                std::cout << "No cron jobs" << std::endl;
                            } else {
                                for (const auto& job : result) {
                                    std::cout << job.value("id", "").substr(0, 8) << "  "
                                              << job.value("schedule", "") << "  "
                                              << job.value("name", "") << "  "
                                              << (job.value("enabled", true) ? "ON" : "OFF")
                                              << std::endl;
                                }
                            }
                        }
                        return 0;
                    }
                } catch (...) {}
                std::cerr << "Gateway not running" << std::endl;
                return 1;
            }

            if (args[0] == "add" && args.size() >= 3) {
                std::string schedule = args[1];
                std::string message;
                for (size_t i = 2; i < args.size(); ++i) {
                    if (!message.empty()) message += " ";
                    message += args[i];
                }
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(3000)) {
                        auto result = client->call("cron.add", {
                            {"schedule", schedule},
                            {"message", message},
                            {"name", message.substr(0, 30)},
                        });
                        client->disconnect();
                        std::cout << "Added: " << result.value("id", "") << std::endl;
                        return 0;
                    }
                } catch (...) {}
                std::cerr << "Gateway not running" << std::endl;
                return 1;
            }

            if (args[0] == "remove" && args.size() >= 2) {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(3000)) {
                        client->call("cron.remove", {{"id", args[1]}});
                        client->disconnect();
                        std::cout << "Removed" << std::endl;
                        return 0;
                    }
                } catch (...) {}
                std::cerr << "Gateway not running" << std::endl;
                return 1;
            }

            std::cerr << "Usage: quantclaw cron [list|add|remove]" << std::endl;
            return 1;
        }
    });

    // --- memory command ---
    cli.add_command({
        "memory",
        "Search and manage memory",
        {},
        [logger](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                std::cerr << "Usage: quantclaw memory <search|status> [query]"
                          << std::endl;
                return 1;
            }

            if (args[0] == "search" && args.size() >= 2) {
                std::string query;
                for (size_t i = 1; i < args.size(); ++i) {
                    if (!query.empty()) query += " ";
                    query += args[i];
                }

                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(3000)) {
                        auto result = client->call("memory.search",
                                                   {{"query", query}});
                        client->disconnect();
                        if (result.is_array()) {
                            for (const auto& r : result) {
                                std::cout << "[" << r.value("source", "") << ":"
                                          << r.value("lineNumber", 0) << "] "
                                          << r.value("content", "").substr(0, 120)
                                          << std::endl;
                            }
                        }
                        return 0;
                    }
                } catch (...) {}

                // Fallback: offline search
                const char* home = std::getenv("HOME");
                std::string home_str = home ? home : "/tmp";
                auto workspace = std::filesystem::path(home_str) /
                                 ".quantclaw/agents/main/workspace";

                quantclaw::MemorySearch search(logger);
                search.index_directory(workspace);
                auto results = search.search(query);
                for (const auto& r : results) {
                    std::cout << "[" << r.source << ":" << r.line_number
                              << "] " << r.content.substr(0, 120) << std::endl;
                }
                return 0;
            }

            if (args[0] == "status") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        "ws://127.0.0.1:18789", "", logger);
                    if (client->connect(3000)) {
                        auto result = client->call("memory.status", {});
                        client->disconnect();
                        std::cout << result.dump(2) << std::endl;
                        return 0;
                    }
                } catch (...) {}
                std::cout << "Gateway not running. Memory status unavailable."
                          << std::endl;
                return 1;
            }

            std::cerr << "Unknown memory subcommand: " << args[0] << std::endl;
            return 1;
        }
    });

    // --- dashboard command ---
    cli.add_command({
        "dashboard",
        "Open the Control UI",
        {},
        [logger](int argc, char** argv) -> int {
            bool no_open = false;
            for (int i = 1; i < argc; ++i) {
                if (std::string(argv[i]) == "--no-open") no_open = true;
            }

            int port = 18790;
            try {
                auto config = quantclaw::QuantClawConfig::load_from_file(
                    quantclaw::QuantClawConfig::default_config_path());
                port = config.gateway.control_ui.port;
            } catch (...) {}

            std::string url = "http://127.0.0.1:" + std::to_string(port) +
                              "/__quantclaw__/control/";
            std::cout << "Dashboard: " << url << std::endl;

            if (!no_open) {
                std::string cmd = "xdg-open '" + url + "' 2>/dev/null || "
                                  "open '" + url + "' 2>/dev/null";
                [[maybe_unused]] int ret = std::system(cmd.c_str());
            }
            return 0;
        }
    });

    // --- logs command ---
    cli.add_command({
        "logs",
        "View gateway logs",
        {},
        [](int argc, char** argv) -> int {
            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            auto log_dir = std::filesystem::path(home_str) / ".quantclaw/logs";

            int lines = 50;
            bool follow = false;
            for (int i = 1; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "-f" || arg == "--follow") follow = true;
                if (arg == "-n" && i + 1 < argc) lines = std::stoi(argv[++i]);
            }

            auto log_file = log_dir / "gateway.log";
            if (!std::filesystem::exists(log_file)) {
                // Try journalctl
                std::string cmd = "journalctl --user -u quantclaw -n " +
                                  std::to_string(lines);
                if (follow) cmd += " -f";
                cmd += " --no-pager 2>/dev/null";
                return std::system(cmd.c_str());
            }

            std::string cmd = follow ? "tail -f " : "tail -n " +
                              std::to_string(lines) + " ";
            cmd += log_file.string();
            return std::system(cmd.c_str());
        }
    });

    return cli.run(argc, argv);
}
