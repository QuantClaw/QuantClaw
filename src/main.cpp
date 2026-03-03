// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "quantclaw/config.hpp"
#include "quantclaw/cli/cli_manager.hpp"
#include "quantclaw/cli/gateway_commands.hpp"
#include "quantclaw/cli/agent_commands.hpp"
#include "quantclaw/cli/session_commands.hpp"
#include "quantclaw/cli/onboard_commands.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/memory_search.hpp"

// Bring port/URL constants into scope (avoids quantclaw:: prefix for literals)
using quantclaw::kDefaultGatewayPort;
using quantclaw::kDefaultGatewayUrl;
using quantclaw::kDefaultHttpPort;

static spdlog::level::level_enum parse_log_level(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "warn")  return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    return spdlog::level::info;
}

static std::shared_ptr<spdlog::logger> create_logger(
    const std::string& log_level = "info",
    const std::string& log_dir = "",
    int log_max_size_mb = 50) {
    auto level = parse_log_level(log_level);

    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console_sink->set_level(level);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    std::vector<spdlog::sink_ptr> sinks{console_sink};

    if (!log_dir.empty()) {
        try {
            std::filesystem::create_directories(log_dir);
            // Divide total cap evenly across 5 rotated files (min 1 MiB each).
            int max_mb = std::max(1, log_max_size_mb);
            std::size_t per_file_bytes =
                static_cast<std::size_t>(std::max(1, max_mb / 5)) * 1024 * 1024;
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_dir + "/quantclaw.log",
                per_file_bytes,
                5);  // keep 5 rotated files
            file_sink->set_level(spdlog::level::debug);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file_sink);
        } catch (const std::exception& e) {
            // Console-only fallback — not fatal.
            std::cerr << "Warning: cannot open log file in " << log_dir
                      << ": " << e.what() << "\n";
        }
    }

    auto logger = std::make_shared<spdlog::logger>(
        "quantclaw", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);  // sinks control their own levels
    spdlog::set_default_logger(logger);
    return logger;
}

int main(int argc, char* argv[]) {
    // Bootstrap with defaults; recreated below once config is loaded.
    auto logger = create_logger();

    // Load config early to derive gateway URL, auth token, and log settings.
    // Silently falls back to defaults if no config exists.
    std::string gateway_url = kDefaultGatewayUrl;
    std::string auth_token;
    try {
        auto cfg = quantclaw::QuantClawConfig::LoadFromFile(
            quantclaw::QuantClawConfig::DefaultConfigPath());
        int port = cfg.gateway.port > 0 ? cfg.gateway.port : kDefaultGatewayPort;
        gateway_url = "ws://127.0.0.1:" + std::to_string(port);
        auth_token = cfg.gateway.auth.token;
        if (auth_token.empty()) {
            const char* env_token = std::getenv("QUANTCLAW_AUTH_TOKEN");
            if (env_token) auth_token = env_token;
        }
        // Rebuild logger with config-specified level and log directory.
        auto cfg_dir = std::filesystem::path(
            quantclaw::QuantClawConfig::DefaultConfigPath()).parent_path();
        logger = create_logger(cfg.system.log_level,
                               (cfg_dir / "logs").string(),
                               cfg.system.log_max_size_mb);
    } catch (...) {
        // No config file yet — defaults are fine.
    }

    // Create shared command handlers
    auto gateway_cmds = std::make_shared<quantclaw::cli::GatewayCommands>(logger);
    auto agent_cmds = std::make_shared<quantclaw::cli::AgentCommands>(logger);
    auto session_cmds = std::make_shared<quantclaw::cli::SessionCommands>(logger);
    auto onboard_cmds = std::make_shared<quantclaw::cli::OnboardCommands>(logger);

    // Propagate to class-based command handlers
    gateway_cmds->SetGatewayUrl(gateway_url);
    agent_cmds->SetGatewayUrl(gateway_url);
    session_cmds->SetGatewayUrl(gateway_url);
    gateway_cmds->SetAuthToken(auth_token);
    agent_cmds->SetAuthToken(auth_token);
    session_cmds->SetAuthToken(auth_token);

    // Build CLI
    quantclaw::cli::CLIManager cli;

    // --- onboard command ---
    cli.AddCommand({
        "onboard",
        "Interactive setup wizard for initial configuration",
        {},
        [onboard_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                return onboard_cmds->OnboardCommand(args);
            }

            std::string sub = args[0];
            std::vector<std::string> sub_args(args.begin() + 1, args.end());

            if (sub == "--install-daemon") {
                return onboard_cmds->InstallDaemonCommand(sub_args);
            }
            if (sub == "--quick") {
                return onboard_cmds->QuickSetupCommand(sub_args);
            }

            // Default: run full wizard
            return onboard_cmds->OnboardCommand(args);
        }
    });

    // --- gateway command ---
    cli.AddCommand({
        "gateway",
        "Manage the Gateway WebSocket server",
        {"g"},
        [gateway_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                // No subcommand: run gateway in foreground
                return gateway_cmds->ForegroundCommand(args);
            }

            std::string sub = args[0];
            std::vector<std::string> sub_args(args.begin() + 1, args.end());

            if (sub == "run")       return gateway_cmds->ForegroundCommand(sub_args);
            if (sub == "install")   return gateway_cmds->InstallCommand(sub_args);
            if (sub == "uninstall") return gateway_cmds->UninstallCommand(sub_args);
            if (sub == "start")     return gateway_cmds->StartCommand(sub_args);
            if (sub == "stop")      return gateway_cmds->StopCommand(sub_args);
            if (sub == "restart")   return gateway_cmds->RestartCommand(sub_args);
            if (sub == "status")    return gateway_cmds->StatusCommand(sub_args);
            if (sub == "call")      return gateway_cmds->CallCommand(sub_args);

            // Flags on direct gateway command �?foreground mode
            if (sub == "--port" || sub == "--foreground" || sub == "--bind") {
                return gateway_cmds->ForegroundCommand(args);
            }

            std::cerr << "Unknown gateway subcommand: " << sub << std::endl;
            std::cerr << "Available: run, install, uninstall, start, stop, "
                         "restart, status, call" << std::endl;
            return 1;
        }
    });

    // --- agent command ---
    cli.AddCommand({
        "agent",
        "Send message to agent via Gateway",
        {"a"},
        [agent_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            // Check for "agent stop" subcommand
            if (!args.empty() && args[0] == "stop") {
                std::vector<std::string> sub_args(args.begin() + 1, args.end());
                return agent_cmds->StopCommand(sub_args);
            }

            return agent_cmds->RequestCommand(args);
        }
    });

    // --- sessions command ---
    cli.AddCommand({
        "sessions",
        "Manage sessions",
        {},
        [session_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                return session_cmds->ListCommand({});
            }

            std::string sub = args[0];
            std::vector<std::string> sub_args(args.begin() + 1, args.end());

            if (sub == "list")    return session_cmds->ListCommand(sub_args);
            if (sub == "history") return session_cmds->HistoryCommand(sub_args);
            if (sub == "delete")  return session_cmds->DeleteCommand(sub_args);
            if (sub == "reset")   return session_cmds->ResetCommand(sub_args);

            std::cerr << "Unknown sessions subcommand: " << sub << std::endl;
            return 1;
        }
    });

    // --- status command (shortcut to gateway.status) ---
    cli.AddCommand({
        "status",
        "Show gateway status",
        {},
        [gateway_cmds](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
            return gateway_cmds->StatusCommand(args);
        }
    });

    // --- health command ---
    cli.AddCommand({
        "health",
        "Gateway health check",
        {},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
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
                    gateway_url, auth_token, logger);
                if (!client->Connect(timeout_ms)) {
                    if (json_output) {
                        std::cout << R"({"status":"unreachable"})" << std::endl;
                    } else {
                        std::cout << "Gateway: unreachable" << std::endl;
                    }
                    return 1;
                }

                auto result = client->Call("gateway.health", {});
                client->Disconnect();

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
    cli.AddCommand({
        "config",
        "Manage configuration",
        {"c"},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            if (args.empty()) {
                std::cerr << "Usage: quantclaw config <get|set|unset|reload|validate|schema> [path] [value]"
                          << std::endl;
                return 1;
            }

            std::string sub = args[0];

            if (sub == "reload") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (!client->Connect(3000)) {
                        std::cerr << "Error: Gateway not running" << std::endl;
                        return 1;
                    }
                    client->Call("config.reload", {});
                    client->Disconnect();
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
                        gateway_url, auth_token, logger);
                    if (!client->Connect(3000)) {
                        // Fallback: read config file directly
                        auto config = quantclaw::QuantClawConfig::LoadFromFile(
                            quantclaw::QuantClawConfig::DefaultConfigPath());
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
                    auto result = client->Call("config.get", params);
                    client->Disconnect();

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
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::SetValue(config_file, path, value);
                    std::cout << path << " = " << value.dump() << std::endl;

                    // Notify running gateway to reload
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(1000)) {
                        client->Call("config.reload", {});
                        client->Disconnect();
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
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::UnsetValue(config_file, path);
                    std::cout << "Removed: " << path << std::endl;

                    // Notify running gateway to reload
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(1000)) {
                        client->Call("config.reload", {});
                        client->Disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "validate") {
                try {
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    auto config = quantclaw::QuantClawConfig::LoadFromFile(config_file);
                    std::cout << "Configuration is valid" << std::endl;
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Configuration is invalid: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "schema") {
                std::cout << "Configuration schema:" << std::endl
                          << "  agent:" << std::endl
                          << "    model: string (default: gpt-4o-mini)" << std::endl
                          << "    maxTokens: integer (default: 4096)" << std::endl
                          << "    maxIterations: integer (default: 100)" << std::endl
                          << "  gateway:" << std::endl
                          << "    port: integer (default: 18800)" << std::endl
                          << "  system:" << std::endl
                          << "    logLevel: string (debug|info|warn|error)" << std::endl;
                return 0;
            }

            std::cerr << "Unknown config subcommand: " << sub << std::endl;
            std::cerr << "Available: get, set, unset, reload, validate, schema" << std::endl;
            return 1;
        }
    });

    // --- skills command ---
    cli.AddCommand({
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
                    auto config = quantclaw::QuantClawConfig::LoadFromFile(
                        quantclaw::QuantClawConfig::DefaultConfigPath());
                    skills_config = config.skills;
                } catch (const std::exception&) {
                    // Use defaults if no config
                }

                auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger);
                auto skills = skill_loader->LoadSkills(skills_config, workspace_path);

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
    cli.AddCommand({
        "doctor",
        "Health check (config, deps, connectivity)",
        {},
        [logger, gateway_url, auth_token](int /*argc*/, char** /*argv*/) -> int {
            std::cout << "QuantClaw Doctor" << std::endl;
            std::cout << std::string(40, '=') << std::endl;

            // Check config file
            std::string config_path = quantclaw::QuantClawConfig::DefaultConfigPath();
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
                    gateway_url, auth_token, logger);
                gw_ok = client->Connect(2000);
                if (gw_ok) client->Disconnect();
            } catch (const std::exception&) {}
            std::cout << "[" << (gw_ok ? "OK" : "!!") << "] Gateway: "
                      << (gw_ok ? "running" : "not running") << std::endl;

            std::cout << std::string(40, '=') << std::endl;
            return (config_ok && ws_ok) ? 0 : 1;
        }
    });

    // --- cron command ---
    cli.AddCommand({
        "cron",
        "Manage scheduled tasks",
        {},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            std::string cron_file = home_str + "/.quantclaw/cron.json";

            if (args.empty() || args[0] == "list") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        auto result = client->Call("cron.list", {});
                        client->Disconnect();
                        // RPC returns {jobs:[...], total, ...}; extract array
                        nlohmann::json jobs_arr = nlohmann::json::array();
                        if (result.is_object() && result.contains("jobs")) {
                            jobs_arr = result["jobs"];
                        } else if (result.is_array()) {
                            jobs_arr = result;
                        }
                        if (jobs_arr.empty()) {
                            std::cout << "No cron jobs" << std::endl;
                        } else {
                            for (const auto& job : jobs_arr) {
                                std::string id = job.value("id", "");
                                std::string sched;
                                if (job.contains("schedule") && job["schedule"].is_object()) {
                                    sched = job["schedule"].value("expr", "");
                                } else {
                                    sched = job.value("schedule", "");
                                }
                                std::cout << id << "  "
                                          << sched << "  "
                                          << job.value("name", "") << "  "
                                          << (job.value("enabled", true) ? "ON" : "OFF")
                                          << std::endl;
                            }
                        }
                        return 0;
                    }
                } catch (const std::exception&) {}
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
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        auto result = client->Call("cron.add", {
                            {"schedule", schedule},
                            {"message", message},
                            {"name", message.substr(0, 30)},
                        });
                        client->Disconnect();
                        std::cout << "Added: " << result.value("id", "") << std::endl;
                        return 0;
                    }
                } catch (const std::exception&) {}
                std::cerr << "Gateway not running" << std::endl;
                return 1;
            }

            if (args[0] == "remove" && args.size() >= 2) {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        client->Call("cron.remove", {{"id", args[1]}});
                        client->Disconnect();
                        std::cout << "Removed" << std::endl;
                        return 0;
                    }
                } catch (const std::exception&) {}
                std::cerr << "Gateway not running" << std::endl;
                return 1;
            }

            std::cerr << "Usage: quantclaw cron [list|add|remove]" << std::endl;
            return 1;
        }
    });

    // --- memory command ---
    cli.AddCommand({
        "memory",
        "Search and manage memory",
        {},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
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
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        auto result = client->Call("memory.search",
                                                   {{"query", query}});
                        client->Disconnect();
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
                } catch (const std::exception&) {}

                // Fallback: offline search
                const char* home = std::getenv("HOME");
                std::string home_str = home ? home : "/tmp";
                auto workspace = std::filesystem::path(home_str) /
                                 ".quantclaw/agents/main/workspace";

                quantclaw::MemorySearch search(logger);
                search.IndexDirectory(workspace);
                auto results = search.Search(query);
                for (const auto& r : results) {
                    std::cout << "[" << r.source << ":" << r.line_number
                              << "] " << r.content.substr(0, 120) << std::endl;
                }
                return 0;
            }

            if (args[0] == "status") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        auto result = client->Call("memory.status", {});
                        client->Disconnect();
                        std::cout << result.dump(2) << std::endl;
                        return 0;
                    }
                } catch (const std::exception&) {}
                std::cout << "Gateway not running. Memory status unavailable."
                          << std::endl;
                return 1;
            }

            std::cerr << "Unknown memory subcommand: " << args[0] << std::endl;
            return 1;
        }
    });

    // --- dashboard command ---
    cli.AddCommand({
        "dashboard",
        "Open the Control UI",
        {},
        [logger](int argc, char** argv) -> int {
            bool no_open = false;
            for (int i = 1; i < argc; ++i) {
                if (std::string(argv[i]) == "--no-open") no_open = true;
            }

            int port = 18801;
            try {
                auto config = quantclaw::QuantClawConfig::LoadFromFile(
                    quantclaw::QuantClawConfig::DefaultConfigPath());
                port = config.gateway.control_ui.port;
            } catch (const std::exception&) {}

            std::string url = "http://127.0.0.1:" + std::to_string(port) +
                              "/__quantclaw__/control/";
            std::cout << "Dashboard: " << url << std::endl;

            if (!no_open) {
#ifdef _WIN32
                std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
                std::string cmd = "open '" + url + "' 2>/dev/null";
#else
                std::string cmd = "xdg-open '" + url + "' 2>/dev/null";
#endif
                [[maybe_unused]] int ret = std::system(cmd.c_str());
            }
            return 0;
        }
    });

    // --- channels command ---
    cli.AddCommand({
        "channels",
        "Manage communication channels",
        {"ch"},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            std::string sub = args.empty() ? "list" : args[0];
            std::vector<std::string> sub_args;
            if (args.size() > 1)
                sub_args.assign(args.begin() + 1, args.end());

            auto make_client = [&logger, &gateway_url, &auth_token]() -> std::shared_ptr<quantclaw::gateway::GatewayClient> {
                auto c = std::make_shared<quantclaw::gateway::GatewayClient>(
                    gateway_url, auth_token, logger);
                if (!c->Connect(3000)) {
                    std::cerr << "Error: Gateway not running" << std::endl;
                    return nullptr;
                }
                return c;
            };

            if (sub == "list") {
                bool json_output = false;
                for (const auto& a : sub_args) {
                    if (a == "--json") json_output = true;
                }
                try {
                    auto client = make_client();
                    if (!client) return 1;
                    auto result = client->Call("channels.list", {});
                    client->Disconnect();
                    if (json_output) {
                        std::cout << result.dump(2) << std::endl;
                    } else {
                        if (result.is_array()) {
                            if (result.empty()) {
                                std::cout << "No channels configured" << std::endl;
                            } else {
                                for (const auto& ch : result) {
                                    std::cout << "  " << ch.value("id", "")
                                              << "  [" << ch.value("type", "") << "]"
                                              << "  " << (ch.value("enabled", false) ? "ON" : "OFF")
                                              << "  " << ch.value("status", "unknown")
                                              << std::endl;
                                }
                            }
                        }
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "status") {
                std::string channel_id = sub_args.empty() ? "" : sub_args[0];
                try {
                    auto client = make_client();
                    if (!client) return 1;
                    nlohmann::json params;
                    if (!channel_id.empty()) params["id"] = channel_id;
                    auto result = client->Call("channels.status", params);
                    client->Disconnect();
                    std::cout << result.dump(2) << std::endl;
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "add") {
                if (sub_args.size() < 2) {
                    std::cerr << "Usage: quantclaw channels add <type> <token> [--id <name>]" << std::endl;
                    return 1;
                }
                std::string type = sub_args[0];
                std::string token = sub_args[1];
                std::string id = type;
                for (size_t i = 2; i < sub_args.size(); ++i) {
                    if (sub_args[i] == "--id" && i + 1 < sub_args.size()) {
                        id = sub_args[++i];
                    }
                }
                try {
                    // Write to config file
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    nlohmann::json channel_json;
                    channel_json["enabled"] = true;
                    channel_json["token"] = token;
                    quantclaw::QuantClawConfig::SetValue(
                        config_file, "channels." + id, channel_json);
                    std::cout << "Added channel: " << id << " (" << type << ")" << std::endl;

                    // Notify gateway to reload
                    auto client = make_client();
                    if (client) {
                        client->Call("config.reload", {});
                        client->Disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "remove") {
                if (sub_args.empty()) {
                    std::cerr << "Usage: quantclaw channels remove <id>" << std::endl;
                    return 1;
                }
                try {
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::UnsetValue(
                        config_file, "channels." + sub_args[0]);
                    std::cout << "Removed channel: " << sub_args[0] << std::endl;

                    auto client = make_client();
                    if (client) {
                        client->Call("config.reload", {});
                        client->Disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "login") {
                if (sub_args.empty()) {
                    std::cerr << "Usage: quantclaw channels login <id>" << std::endl;
                    return 1;
                }
                try {
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::SetValue(
                        config_file, "channels." + sub_args[0] + ".enabled", true);
                    std::cout << "Enabled channel: " << sub_args[0] << std::endl;

                    auto client = make_client();
                    if (client) {
                        client->Call("config.reload", {});
                        client->Disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "logout") {
                if (sub_args.empty()) {
                    std::cerr << "Usage: quantclaw channels logout <id>" << std::endl;
                    return 1;
                }
                try {
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::SetValue(
                        config_file, "channels." + sub_args[0] + ".enabled", false);
                    std::cout << "Disabled channel: " << sub_args[0] << std::endl;

                    auto client = make_client();
                    if (client) {
                        client->Call("config.reload", {});
                        client->Disconnect();
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            std::cerr << "Unknown channels subcommand: " << sub << std::endl;
            std::cerr << "Available: list, status, add, remove, login, logout" << std::endl;
            return 1;
        }
    });

    // --- models command ---
    cli.AddCommand({
        "models",
        "Manage AI models",
        {"m"},
        [logger, gateway_url, auth_token](int argc, char** argv) -> int {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

            std::string sub = args.empty() ? "list" : args[0];
            std::vector<std::string> sub_args;
            if (args.size() > 1)
                sub_args.assign(args.begin() + 1, args.end());

            if (sub == "list") {
                bool json_output = false;
                for (const auto& a : sub_args) {
                    if (a == "--json") json_output = true;
                }
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (!client->Connect(3000)) {
                        // Fallback: show configured model from config file
                        auto config = quantclaw::QuantClawConfig::LoadFromFile(
                            quantclaw::QuantClawConfig::DefaultConfigPath());
                        std::cout << "Current model: " << config.agent.model << std::endl;
                        std::cout << "(Gateway not running, showing config only)" << std::endl;
                        return 0;
                    }
                    auto result = client->Call("models.list", {});
                    client->Disconnect();
                    if (json_output) {
                        std::cout << result.dump(2) << std::endl;
                    } else {
                        if (result.contains("current")) {
                            std::cout << "Current: " << result["current"].get<std::string>() << std::endl;
                        }
                        // New format: models array with metadata
                        if (result.contains("models") && result["models"].is_array()) {
                            std::cout << "\nAvailable models:" << std::endl;
                            for (const auto& m : result["models"]) {
                                bool active = m.value("active", false);
                                std::string id = m.value("id", "");
                                std::string prov = m.value("provider", "");
                                std::string name = m.value("name", "");
                                int ctx = m.value("contextWindow", 0);
                                bool reasoning = m.value("reasoning", false);

                                std::cout << (active ? "  * " : "    ");
                                std::cout << id << " (" << prov << ")";
                                if (!name.empty() && name != id) {
                                    std::cout << " \xe2\x80\x94 " << name;
                                }
                                if (ctx > 0) {
                                    std::cout << ", " << (ctx / 1000) << "K context";
                                }
                                if (reasoning) {
                                    std::cout << " [reasoning]";
                                }
                                // Check for image input
                                if (m.contains("input") && m["input"].is_array()) {
                                    for (const auto& inp : m["input"]) {
                                        if (inp.is_string() && inp.get<std::string>() == "image") {
                                            std::cout << " [image]";
                                            break;
                                        }
                                    }
                                }
                                if (active) std::cout << "   [active]";
                                std::cout << std::endl;
                            }
                        } else if (result.contains("providers") && result["providers"].is_array()) {
                            // Legacy format fallback
                            std::cout << "\nProviders:" << std::endl;
                            for (const auto& p : result["providers"]) {
                                std::cout << "  " << p.value("id", "")
                                          << " (" << p.value("type", "") << ")"
                                          << std::endl;
                                if (p.contains("models") && p["models"].is_array()) {
                                    for (const auto& m : p["models"]) {
                                        std::cout << "    - " << m.get<std::string>() << std::endl;
                                    }
                                }
                            }
                        }

                        // Show aliases
                        if (result.contains("aliases") && result["aliases"].is_object() &&
                            !result["aliases"].empty()) {
                            std::cout << "\nAliases:" << std::endl;
                            for (auto& [alias, target] : result["aliases"].items()) {
                                std::cout << "  " << alias << " -> " << target.get<std::string>() << std::endl;
                            }
                        }
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "set") {
                if (sub_args.empty()) {
                    std::cerr << "Usage: quantclaw models set <model>" << std::endl;
                    return 1;
                }
                std::string model = sub_args[0];
                try {
                    // Write to config file
                    auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
                    quantclaw::QuantClawConfig::SetValue(
                        config_file, "agent.model", model);

                    // Also update running gateway via RPC
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (client->Connect(3000)) {
                        client->Call("models.set", {{"model", model}});
                        client->Disconnect();
                    }
                    std::cout << "Model set to: " << model << std::endl;
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            if (sub == "aliases") {
                try {
                    auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                        gateway_url, auth_token, logger);
                    if (!client->Connect(3000)) {
                        std::cerr << "Gateway not running" << std::endl;
                        return 1;
                    }
                    auto result = client->Call("models.list", {});
                    client->Disconnect();
                    if (result.contains("aliases") && result["aliases"].is_object()) {
                        for (auto& [alias, target] : result["aliases"].items()) {
                            std::cout << "  " << alias << " -> " << target.get<std::string>() << std::endl;
                        }
                    } else {
                        std::cout << "No model aliases configured" << std::endl;
                    }
                    return 0;
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }
            }

            std::cerr << "Unknown models subcommand: " << sub << std::endl;
            std::cerr << "Available: list, set, aliases" << std::endl;
            return 1;
        }
    });

    // --- logs command ---
    cli.AddCommand({
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

    return cli.Run(argc, argv);
}
