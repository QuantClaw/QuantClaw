// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

import std;
import nlohmann.json;

import quantclaw.core.skill_loader;
import quantclaw.core.memory_search;
import quantclaw.cli.agent_commands;
import quantclaw.cli.cli_manager;
import quantclaw.cli.gateway_commands;
import quantclaw.cli.onboard_commands;
import quantclaw.cli.session_commands;
import quantclaw.common.parse_util;
import quantclaw.config;
import quantclaw.constants;
import quantclaw.gateway.gateway_client;
import quantclaw.platform.process;

using Json = decltype(std::declval<quantclaw::gateway::GatewayClient>().Call(
  std::declval<const std::string&>(), {}));

// Bring port/URL constants into scope (avoids quantclaw:: prefix for literals)
using quantclaw::kDefaultGatewayPort;
using quantclaw::kDefaultGatewayUrl;
using quantclaw::kDefaultHttpPort;

static spdlog::level::level_enum parse_log_level(const std::string& s) {
  if (s == "trace")
    return spdlog::level::trace;
  if (s == "debug")
    return spdlog::level::debug;
  if (s == "warn")
    return spdlog::level::warn;
  if (s == "error")
    return spdlog::level::err;
  return spdlog::level::info;
}

// Enforce total log storage cap by removing the oldest rotated files that
// exceed log_max_size_mb.  Called once at startup after the daily sink is
// created — not on every write — to keep overhead negligible.
static void enforce_log_size_cap(const std::string& log_dir, int max_size_mb) {
  if (max_size_mb <= 0)
    return;  // 0 = unlimited

  namespace fs = std::filesystem;
  std::vector<fs::directory_entry> log_files;
  try {
    for (const auto& entry : fs::directory_iterator(log_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".log") {
        log_files.push_back(entry);
      }
    }
  } catch (...) {
    return;  // directory not readable — nothing to trim
  }

  // Sort oldest-first by last-write-time.
  // Use error_code overloads so externally-deleted files don't throw.
  std::sort(log_files.begin(), log_files.end(),
            [](const fs::directory_entry& a, const fs::directory_entry& b) {
              std::error_code ea, eb;
              auto ta = fs::last_write_time(a, ea);
              auto tb = fs::last_write_time(b, eb);
              if (ea) return true;   // errors sort first (will be removed)
              if (eb) return false;
              return ta < tb;
            });

  // Sum total size and remove oldest until within budget.
  std::uintmax_t total = 0;
  for (const auto& f : log_files) {
    std::error_code ec;
    auto sz = fs::file_size(f, ec);
    if (!ec)
      total += sz;
  }

  auto cap = static_cast<std::uintmax_t>(max_size_mb) * 1024 * 1024;
  for (size_t i = 0; i < log_files.size() && total > cap; ++i) {
    std::error_code ec;
    auto sz = fs::file_size(log_files[i], ec);
    if (ec) continue;
    fs::remove(log_files[i].path(), ec);
    if (!ec)
      total -= sz;
  }
}

static std::shared_ptr<spdlog::logger>
create_logger(const std::string& log_level = "info",
              const std::string& log_dir = "", int log_retain_days = 7,
              int log_max_size_mb = 50) {
  auto level = parse_log_level(log_level);

  auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  console_sink->set_level(level);
  console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  std::vector<spdlog::sink_ptr> sinks{console_sink};

  if (!log_dir.empty()) {
    try {
      std::filesystem::create_directories(log_dir);
      // Rotate at midnight; keep log_retain_days worth of files (0 =
      // unlimited).
      int retain = std::clamp(log_retain_days, 0, 65535);
      auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
          log_dir + "/quantclaw.log", 0, 0,  // rotate at 00:00 local time
          false,                             // truncate = false (append)
          static_cast<uint16_t>(retain));
      file_sink->set_level(spdlog::level::debug);
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
      sinks.push_back(file_sink);

      // Enforce total storage cap at startup.
      enforce_log_size_cap(log_dir, log_max_size_mb);
    } catch (const std::exception& e) {
      // Console-only fallback — not fatal.
      std::cerr << "Warning: cannot open log file in " << log_dir << ": "
                << e.what() << "\n";
    }
  }

  auto logger =
      std::make_shared<spdlog::logger>("quantclaw", sinks.begin(), sinks.end());
  logger->set_level(spdlog::level::trace);  // sinks control their own levels
  spdlog::set_default_logger(logger);
  return logger;
}

int main(int argc, char* argv[]) {
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
      if (env_token)
        auth_token = env_token;
    }
    // Rebuild logger with config-specified level and log directory.
    auto cfg_dir =
        std::filesystem::path(quantclaw::QuantClawConfig::DefaultConfigPath())
            .parent_path();
    logger = create_logger(cfg.system.log_level, (cfg_dir / "logs").string(),
                           cfg.system.log_retention_days,
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
  cli.AddCommand({"onboard",
                  "Interactive setup wizard for initial configuration",
                  {},
                  [onboard_cmds](int argc, char** argv) -> int {
                    std::vector<std::string> args;
                    for (int i = 1; i < argc; ++i)
                      args.push_back(argv[i]);

                    if (args.empty()) {
                      return onboard_cmds->OnboardCommand(args);
                    }

                    std::string sub = args[0];
                    std::vector<std::string> sub_args(args.begin() + 1,
                                                      args.end());

                    if (sub == "--install-daemon") {
                      return onboard_cmds->InstallDaemonCommand(sub_args);
                    }
                    if (sub == "--quick") {
                      return onboard_cmds->QuickSetupCommand(sub_args);
                    }

                    // Default: run full wizard
                    return onboard_cmds->OnboardCommand(args);
                  }});

  // --- gateway command ---
  cli.AddCommand(
      {"gateway",
       "Manage the Gateway WebSocket server",
       {"g"},
       [gateway_cmds](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         if (args.empty()) {
           // No subcommand: run gateway in foreground
           return gateway_cmds->ForegroundCommand(args);
         }

         std::string sub = args[0];
         std::vector<std::string> sub_args(args.begin() + 1, args.end());

         if (sub == "run")
           return gateway_cmds->ForegroundCommand(sub_args);
         if (sub == "install")
           return gateway_cmds->InstallCommand(sub_args);
         if (sub == "uninstall")
           return gateway_cmds->UninstallCommand(sub_args);
         if (sub == "start")
           return gateway_cmds->StartCommand(sub_args);
         if (sub == "stop")
           return gateway_cmds->StopCommand(sub_args);
         if (sub == "restart")
           return gateway_cmds->RestartCommand(sub_args);
         if (sub == "status")
           return gateway_cmds->StatusCommand(sub_args);
         if (sub == "call")
           return gateway_cmds->CallCommand(sub_args);

         // Flags on direct gateway command �?foreground mode
         if (sub == "--port" || sub == "--foreground" || sub == "--bind") {
           return gateway_cmds->ForegroundCommand(args);
         }

         std::cerr << "Unknown gateway subcommand: " << sub << '\n';
         std::cerr << "Available: run, install, uninstall, start, stop, "
                      "restart, status, call"
                   << '\n';
         return 1;
       }});

  // --- agent command ---
  cli.AddCommand({"agent",
                  "Send message to agent via Gateway",
                  {"a"},
                  [agent_cmds](int argc, char** argv) -> int {
                    std::vector<std::string> args;
                    for (int i = 1; i < argc; ++i)
                      args.push_back(argv[i]);

                    // Check for "agent stop" subcommand
                    if (!args.empty() && args[0] == "stop") {
                      std::vector<std::string> sub_args(args.begin() + 1,
                                                        args.end());
                      return agent_cmds->StopCommand(sub_args);
                    }

                    return agent_cmds->RequestCommand(args);
                  }});

  // --- sessions command ---
  cli.AddCommand(
      {"sessions",
       "Manage sessions",
       {},
       [session_cmds](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         if (args.empty()) {
           return session_cmds->ListCommand({});
         }

         std::string sub = args[0];
         std::vector<std::string> sub_args(args.begin() + 1, args.end());

         if (sub == "list")
           return session_cmds->ListCommand(sub_args);
         if (sub == "history")
           return session_cmds->HistoryCommand(sub_args);
         if (sub == "delete")
           return session_cmds->DeleteCommand(sub_args);
         if (sub == "reset")
           return session_cmds->ResetCommand(sub_args);

         std::cerr << "Unknown sessions subcommand: " << sub << '\n';
         return 1;
       }});

  // --- status command (shortcut to gateway.status) ---
  cli.AddCommand({"status",
                  "Show gateway status",
                  {},
                  [gateway_cmds](int argc, char** argv) -> int {
                    std::vector<std::string> args;
                    for (int i = 1; i < argc; ++i)
                      args.push_back(argv[i]);
                    return gateway_cmds->StatusCommand(args);
                  }});

  // --- health command ---
  cli.AddCommand(
      {"health",
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
               std::cout << R"({"status":"unreachable"})" << '\n';
             } else {
               std::cout << "Gateway: unreachable" << '\n';
             }
             return 1;
           }

           auto result = client->Call("gateway.health", {});
           client->Disconnect();

           if (json_output) {
             std::cout << result.dump(2) << '\n';
           } else {
             std::cout << "Gateway: " << result.value("status", "unknown")
                       << '\n';
             std::cout << "Version: " << result.value("version", "unknown")
                       << '\n';
             std::cout << "Uptime:  " << result.value("uptime", 0) << "s"
                       << '\n';
           }
           return 0;
         } catch (const std::exception& e) {
           std::cerr << "Error: " << e.what() << '\n';
           return 1;
         }
       }});

  // --- config command ---
  cli.AddCommand(
      {"config",
       "Manage configuration",
       {"c"},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         if (args.empty()) {
           std::cerr << "Usage: quantclaw config "
                        "<get|set|unset|reload|validate|schema> [path] [value]"
                     << '\n';
           return 1;
         }

         std::string sub = args[0];

         if (sub == "reload") {
           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (!client->Connect(3000)) {
               std::cerr << "Error: Gateway not running" << '\n';
               return 1;
             }
             client->Call("config.reload", {});
             client->Disconnect();
             std::cout << "Configuration reloaded" << '\n';
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "get") {
           std::string path = args.size() > 1 ? args[1] : "";
           bool json_output = false;
           for (const auto& a : args) {
             if (a == "--json")
               json_output = true;
           }

           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (!client->Connect(3000)) {
               // Fallback: read config file directly
               auto config = quantclaw::QuantClawConfig::LoadFromFile(
                   quantclaw::QuantClawConfig::DefaultConfigPath());
               if (path == "gateway.port") {
                 std::cout << config.gateway.port << '\n';
               } else if (path == "agent.model") {
                 std::cout << config.agent.model << '\n';
               } else {
                 std::cerr << "Gateway not running. Limited config access."
                           << '\n';
               }
               return 0;
             }

             Json params = Json::object();
             if (!path.empty())
               params["path"] = path;
             auto result = client->Call("config.get", params);
             client->Disconnect();

             if (json_output) {
               std::cout << result.dump(2) << '\n';
             } else {
               if (result.is_primitive()) {
                 std::cout << result << '\n';
               } else {
                 std::cout << result.dump(2) << '\n';
               }
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "set") {
           if (args.size() < 3) {
             std::cerr << "Usage: quantclaw config set <path> <value>"
                       << '\n';
             return 1;
           }
           std::string path = args[1];
           std::string raw_value = args[2];

           // Parse value: try JSON first, then treat as string
           Json value = Json::object();
           try {
             value = Json::parse(raw_value);
           } catch (const std::exception&) {
             value = raw_value;
           }

           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::SetValue(config_file, path, value);
             std::cout << path << " = " << value.dump() << '\n';

             // Notify running gateway to reload
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(1000)) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "unset") {
           if (args.size() < 2) {
             std::cerr << "Usage: quantclaw config unset <path>" << '\n';
             return 1;
           }
           std::string path = args[1];

           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::UnsetValue(config_file, path);
             std::cout << "Removed: " << path << '\n';

             // Notify running gateway to reload
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(1000)) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "validate") {
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             auto config =
                 quantclaw::QuantClawConfig::LoadFromFile(config_file);
             std::cout << "Configuration is valid" << '\n';
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Configuration is invalid: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "schema") {
           std::cout << "Configuration schema:" << '\n'
                     << "  agent:" << '\n'
                     << "    model: string (default: gpt-4o-mini)" << '\n'
                     << "    maxTokens: integer (default: 4096)" << '\n'
                     << "    maxIterations: integer (default: 100)" << '\n'
                     << "  gateway:" << '\n'
                     << "    port: integer (default: 18800)" << '\n'
                     << "  system:" << '\n'
                     << "    logLevel: string (debug|info|warn|error)"
                     << '\n';
           return 0;
         }

         std::cerr << "Unknown config subcommand: " << sub << '\n';
         std::cerr << "Available: get, set, unset, reload, validate, schema"
                   << '\n';
         return 1;
       }});

  // --- skills command ---
  cli.AddCommand(
      {"skills",
       "Manage agent skills",
       {"s"},
       [logger](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         std::string sub = args.empty() ? "list" : args[0];

         if (sub == "list") {
           // Multi-directory skill loading
           std::string home_str;
           const char* home = std::getenv("HOME");
           if (home)
             home_str = home;
           else
             home_str = "/tmp";

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
           auto skills =
               skill_loader->LoadSkills(skills_config, workspace_path);

           if (skills.empty()) {
             std::cout << "No skills found" << '\n';
           } else {
             std::cout << "Skills (" << skills.size() << "):" << '\n';
             for (const auto& skill : skills) {
               std::cout << "  ";
               if (!skill.emoji.empty())
                 std::cout << skill.emoji << " ";
               std::cout << skill.name;
               if (!skill.description.empty()) {
                 std::cout << " - " << skill.description;
               }
               std::cout << '\n';
             }
           }
           return 0;
         }

         if (sub == "install") {
           if (args.size() < 2) {
             std::cerr << "Usage: quantclaw skills install <name>" << '\n';
             return 1;
           }
           const std::string& skill_name = args[1];

           std::string home_str;
           const char* home = std::getenv("HOME");
           if (home)
             home_str = home;
           else
             home_str = "/tmp";

           auto workspace_path = std::filesystem::path(home_str) /
                                 ".quantclaw/agents/main/workspace";

           quantclaw::SkillsConfig skills_config;
           try {
             auto config = quantclaw::QuantClawConfig::LoadFromFile(
                 quantclaw::QuantClawConfig::DefaultConfigPath());
             skills_config = config.skills;
           } catch (const std::exception&) {}

           auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger);
           auto skills =
               skill_loader->LoadSkills(skills_config, workspace_path);

           auto it = std::find_if(skills.begin(), skills.end(),
                                  [&](const quantclaw::SkillMetadata& s) {
                                    return s.name == skill_name;
                                  });
           if (it == skills.end()) {
             std::cerr << "Skill not found: " << skill_name << '\n';
             return 1;
           }

           std::cout << "Installing dependencies for skill: " << skill_name
                     << '\n';
           bool ok = skill_loader->InstallSkill(*it);
           if (ok) {
             std::cout << "Done." << '\n';
             return 0;
           } else {
             std::cerr << "Some dependencies failed to install." << '\n';
             return 1;
           }
         }

         std::cerr << "Unknown skills subcommand: " << sub << '\n';
         std::cerr << "Available: list, install <name>" << '\n';
         return 1;
       }});

  // --- doctor command ---
  cli.AddCommand(
      {"doctor",
       "Health check (config, deps, connectivity)",
       {},
       [logger, gateway_url, auth_token](int /*argc*/, char** /*argv*/) -> int {
         std::cout << "QuantClaw Doctor" << '\n';
         std::cout << std::string(40, '=') << '\n';

         // Check config file
         std::string config_path =
             quantclaw::QuantClawConfig::DefaultConfigPath();
         bool config_ok = std::filesystem::exists(config_path);
         std::cout << "[" << (config_ok ? "OK" : "!!")
                   << "] Config file: " << config_path << '\n';

         // Check workspace
         const char* home = std::getenv("HOME");
         std::string home_str = home ? home : "/tmp";
         auto workspace = std::filesystem::path(home_str) /
                          ".quantclaw/agents/main/workspace";
         bool ws_ok = std::filesystem::exists(workspace);
         std::cout << "[" << (ws_ok ? "OK" : "!!")
                   << "] Workspace: " << workspace.string() << '\n';

         // Check SOUL.md
         auto soul_path = workspace / "SOUL.md";
         bool soul_ok = std::filesystem::exists(soul_path);
         std::cout << "[" << (soul_ok ? "OK" : "--") << "] SOUL.md"
                   << '\n';

         // Check gateway connectivity
         bool gw_ok = false;
         try {
           auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
               gateway_url, auth_token, logger);
           gw_ok = client->Connect(2000);
           if (gw_ok)
             client->Disconnect();
         } catch (const std::exception&) {}
         std::cout << "[" << (gw_ok ? "OK" : "!!")
                   << "] Gateway: " << (gw_ok ? "running" : "not running")
                   << '\n';

         std::cout << std::string(40, '=') << '\n';
         return (config_ok && ws_ok) ? 0 : 1;
       }});

  // --- cron command ---
  cli.AddCommand(
      {"cron",
       "Manage scheduled tasks",
       {},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

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
               Json jobs_arr = Json::array();
               if (result.is_object() && result.contains("jobs")) {
                 jobs_arr = result["jobs"];
               } else if (result.is_array()) {
                 jobs_arr = result;
               }
               if (jobs_arr.empty()) {
                 std::cout << "No cron jobs" << '\n';
               } else {
                 for (const auto& job : jobs_arr) {
                   std::string id = job.value("id", "");
                   std::string sched;
                   if (job.contains("schedule") &&
                       job["schedule"].is_object()) {
                     sched = job["schedule"].value("expr", "");
                   } else {
                     sched = job.value("schedule", "");
                   }
                   std::cout << id << "  " << sched << "  "
                             << job.value("name", "") << "  "
                             << (job.value("enabled", true) ? "ON" : "OFF")
                             << '\n';
                 }
               }
               return 0;
             }
           } catch (const std::exception&) {}
           std::cerr << "Gateway not running" << '\n';
           return 1;
         }

         if (args[0] == "add" && args.size() >= 3) {
           // Supported forms:
           //   cron add <schedule> <message...>        (2+ args after "add")
           //   cron add <name> <schedule> <message...> (3+ args — name first)
           std::string name, schedule, message;
           if (args.size() >= 4) {
             // 3-arg form: name schedule message...
             name = args[1];
             schedule = args[2];
             for (size_t i = 3; i < args.size(); ++i) {
               if (!message.empty())
                 message += " ";
               message += args[i];
             }
           } else {
             // 2-arg form: schedule message
             schedule = args[1];
             message = args[2];
           }
           if (name.empty())
             name = message.substr(0, 30);
           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(3000)) {
               auto result =
                   client->Call("cron.add", {
                                                {"schedule", schedule},
                                                {"message", message},
                                                {"name", name},
                                            });
               client->Disconnect();
               std::cout << "Added: " << result.value("id", "") << '\n';
               return 0;
             }
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
           std::cerr << "Error: Gateway not running" << '\n';
           return 1;
         }

         if (args[0] == "remove" && args.size() >= 2) {
           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(3000)) {
               client->Call("cron.remove", {{"id", args[1]}});
               client->Disconnect();
               std::cout << "Removed" << '\n';
               return 0;
             }
           } catch (const std::exception&) {}
           std::cerr << "Gateway not running" << '\n';
           return 1;
         }

         std::cerr << "Usage: quantclaw cron [list|add|remove]" << '\n';
         return 1;
       }});

  // --- memory command ---
  cli.AddCommand(
      {"memory",
       "Search and manage memory",
       {},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         if (args.empty()) {
           std::cerr << "Usage: quantclaw memory <search|status> [query]"
                     << '\n';
           return 1;
         }

         if (args[0] == "search" && args.size() >= 2) {
           std::string query;
           for (size_t i = 1; i < args.size(); ++i) {
             if (!query.empty())
               query += " ";
             query += args[i];
           }

           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(3000)) {
               auto result = client->Call("memory.search", {{"query", query}});
               client->Disconnect();
               if (result.is_array()) {
                 for (const auto& r : result) {
                   std::cout << "[" << r.value("source", "") << ":"
                             << r.value("lineNumber", 0) << "] "
                             << r.value("content", "").substr(0, 120)
                             << '\n';
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
             std::cout << "[" << r.source << ":" << r.line_number << "] "
                       << r.content.substr(0, 120) << '\n';
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
               std::cout << result.dump(2) << '\n';
               return 0;
             }
           } catch (const std::exception&) {}
           std::cout << "Gateway not running. Memory status unavailable."
                     << '\n';
           return 1;
         }

         std::cerr << "Unknown memory subcommand: " << args[0] << '\n';
         return 1;
       }});

  // --- dashboard command ---
  cli.AddCommand({"dashboard",
                  "Open the Control UI",
                  {},
                  [logger](int argc, char** argv) -> int {
                    bool no_open = false;
                    for (int i = 1; i < argc; ++i) {
                      if (std::string(argv[i]) == "--no-open")
                        no_open = true;
                    }

                    int port = 18801;
                    try {
                      auto config = quantclaw::QuantClawConfig::LoadFromFile(
                          quantclaw::QuantClawConfig::DefaultConfigPath());
                      port = config.gateway.control_ui.port;
                    } catch (const std::exception&) {}

                    std::string url =
                        "http://127.0.0.1:" + std::to_string(port) +
                        "/__quantclaw__/control/";
                    std::cout << "Dashboard: " << url << '\n';

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
                  }});

  // --- channels command ---
  cli.AddCommand(
      {"channels",
       "Manage communication channels",
       {"ch"},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         std::string sub = args.empty() ? "list" : args[0];
         std::vector<std::string> sub_args;
         if (args.size() > 1)
           sub_args.assign(args.begin() + 1, args.end());

         auto make_client = [&logger, &gateway_url, &auth_token]()
             -> std::shared_ptr<quantclaw::gateway::GatewayClient> {
           auto c = std::make_shared<quantclaw::gateway::GatewayClient>(
               gateway_url, auth_token, logger);
           if (!c->Connect(3000)) {
             std::cerr << "Error: Gateway not running" << '\n';
             return nullptr;
           }
           return c;
         };

         if (sub == "list") {
           bool json_output = false;
           for (const auto& a : sub_args) {
             if (a == "--json")
               json_output = true;
           }
           try {
             auto client = make_client();
             if (!client)
               return 1;
             auto result = client->Call("channels.list", {});
             client->Disconnect();
             if (json_output) {
               std::cout << result.dump(2) << '\n';
             } else {
               if (result.is_array()) {
                 if (result.empty()) {
                   std::cout << "No channels configured" << '\n';
                 } else {
                   for (const auto& ch : result) {
                     std::cout << "  " << ch.value("id", "") << "  ["
                               << ch.value("type", "") << "]" << "  "
                               << (ch.value("enabled", false) ? "ON" : "OFF")
                               << "  " << ch.value("status", "unknown")
                               << '\n';
                   }
                 }
               }
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "status") {
           std::string channel_id = sub_args.empty() ? "" : sub_args[0];
           try {
             auto client = make_client();
             if (!client)
               return 1;
             Json params = Json::object();
             if (!channel_id.empty())
               params["id"] = channel_id;
             auto result = client->Call("channels.status", params);
             client->Disconnect();
             std::cout << result.dump(2) << '\n';
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "add") {
           if (sub_args.size() < 2) {
             std::cerr
                 << "Usage: quantclaw channels add <type> <token> [--id <name>]"
                 << '\n';
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
             Json channel_json = Json::object();
             channel_json["enabled"] = true;
             channel_json["token"] = token;
             quantclaw::QuantClawConfig::SetValue(config_file, "channels." + id,
                                                  channel_json);
             std::cout << "Added channel: " << id << " (" << type << ")"
                       << '\n';

             // Notify gateway to reload
             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "remove") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw channels remove <id>" << '\n';
             return 1;
           }
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::UnsetValue(config_file,
                                                    "channels." + sub_args[0]);
             std::cout << "Removed channel: " << sub_args[0] << '\n';

             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "login") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw channels login <id>" << '\n';
             return 1;
           }
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::SetValue(
                 config_file, "channels." + sub_args[0] + ".enabled", true);
             std::cout << "Enabled channel: " << sub_args[0] << '\n';

             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "logout") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw channels logout <id>" << '\n';
             return 1;
           }
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::SetValue(
                 config_file, "channels." + sub_args[0] + ".enabled", false);
             std::cout << "Disabled channel: " << sub_args[0] << '\n';

             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         std::cerr << "Unknown channels subcommand: " << sub << '\n';
         std::cerr << "Available: list, status, add, remove, login, logout"
                   << '\n';
         return 1;
       }});

  // --- models command ---
  cli.AddCommand(
      {"models",
       "Manage AI models",
       {"m"},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         std::string sub = args.empty() ? "list" : args[0];
         std::vector<std::string> sub_args;
         if (args.size() > 1)
           sub_args.assign(args.begin() + 1, args.end());

         if (sub == "list") {
           bool json_output = false;
           for (const auto& a : sub_args) {
             if (a == "--json")
               json_output = true;
           }
           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (!client->Connect(3000)) {
               // Fallback: show configured model from config file
               auto config = quantclaw::QuantClawConfig::LoadFromFile(
                   quantclaw::QuantClawConfig::DefaultConfigPath());
               std::cout << "Current model: " << config.agent.model
                         << '\n';
               std::cout << "(Gateway not running, showing config only)"
                         << '\n';
               return 0;
             }
             auto result = client->Call("models.list", {});
             client->Disconnect();
             if (json_output) {
               std::cout << result.dump(2) << '\n';
             } else {
               if (result.contains("current")) {
                 std::cout << "Current: "
                           << result["current"].get<std::string>() << '\n';
               }
               // New format: models array with metadata
               if (result.contains("models") && result["models"].is_array()) {
                 std::cout << "\nAvailable models:" << '\n';
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
                       if (inp.is_string() &&
                           inp.get<std::string>() == "image") {
                         std::cout << " [image]";
                         break;
                       }
                     }
                   }
                   if (active)
                     std::cout << "   [active]";
                   std::cout << '\n';
                 }
               } else if (result.contains("providers") &&
                          result["providers"].is_array()) {
                 // Legacy format fallback
                 std::cout << "\nProviders:" << '\n';
                 for (const auto& p : result["providers"]) {
                   std::cout << "  " << p.value("id", "") << " ("
                             << p.value("type", "") << ")" << '\n';
                   if (p.contains("models") && p["models"].is_array()) {
                     for (const auto& m : p["models"]) {
                       std::cout << "    - " << m.get<std::string>()
                                 << '\n';
                     }
                   }
                 }
               }

               // Show aliases
               if (result.contains("aliases") &&
                   result["aliases"].is_object() &&
                   !result["aliases"].empty()) {
                 std::cout << "\nAliases:" << '\n';
                 for (const auto& item : result["aliases"].items()) {
                   std::cout << "  " << item.key() << " -> "
                             << item.value().get<std::string>() << '\n';
                 }
               }
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "set") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw models set <model>" << '\n';
             return 1;
           }
           std::string model = sub_args[0];
           try {
             // Write to config file
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::SetValue(config_file, "agent.model",
                                                  model);

             // Also update running gateway via RPC
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (client->Connect(3000)) {
               client->Call("models.set", {{"model", model}});
               client->Disconnect();
             }
             std::cout << "Model set to: " << model << '\n';
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "aliases") {
           try {
             auto client = std::make_shared<quantclaw::gateway::GatewayClient>(
                 gateway_url, auth_token, logger);
             if (!client->Connect(3000)) {
               std::cerr << "Gateway not running" << '\n';
               return 1;
             }
             auto result = client->Call("models.list", {});
             client->Disconnect();
             if (result.contains("aliases") && result["aliases"].is_object()) {
               for (const auto& item : result["aliases"].items()) {
                 std::cout << "  " << item.key() << " -> "
                           << item.value().get<std::string>() << '\n';
               }
             } else {
               std::cout << "No model aliases configured" << '\n';
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         std::cerr << "Unknown models subcommand: " << sub << '\n';
         std::cerr << "Available: list, set, aliases" << '\n';
         return 1;
       }});

  // --- logs command ---
  cli.AddCommand(
      {"logs", "View gateway logs", {}, [](int argc, char** argv) -> int {
         auto home_dir = quantclaw::platform::home_directory();
         auto log_dir = std::filesystem::path(home_dir) / ".quantclaw" / "logs";

         int lines = 50;
         bool follow = false;
         for (int i = 1; i < argc; ++i) {
           std::string arg = argv[i];
           if (arg == "-f" || arg == "--follow") {
             follow = true;
           }
           if (arg == "-n" && i + 1 < argc) {
             if (auto parsed = quantclaw::ParsePositiveInt(argv[++i])) {
               lines = *parsed;
             } else {
               std::cerr << "Invalid value for -n: " << argv[i] << '\n';
               return 1;
             }
           }
         }

         auto log_file = log_dir / "gateway.log";
         if (!std::filesystem::exists(log_file)) {
#ifdef _WIN32
           std::cerr << "No log file found at: " << log_file.string()
                     << '\n';
           std::cerr << "Start the gateway first: quantclaw gateway run"
                     << '\n';
           return 1;
#else
           // Try journalctl on Linux
           std::string cmd =
               "journalctl --user -u quantclaw -n " +
               std::to_string(lines);
           if (follow) {
             cmd += " -f";
           }
           cmd += " --no-pager 2>/dev/null";
           return std::system(cmd.c_str());
#endif
         }

#ifdef _WIN32
         // Windows: read the file directly (no tail)
         if (follow) {
           std::cerr << "Follow mode (-f) is not supported "
                        "on Windows"
                     << '\n';
           return 1;
         }
         std::ifstream f(log_file);
         if (!f.is_open()) {
           std::cerr << "Cannot open: " << log_file.string() << '\n';
           return 1;
         }
         std::deque<std::string> tail_lines;
         std::string line;
         while (std::getline(f, line)) {
           tail_lines.push_back(line);
           if (static_cast<int>(tail_lines.size()) > lines) {
             tail_lines.pop_front();
           }
         }
         for (const auto& l : tail_lines) {
           std::cout << l << "\n";
         }
         return 0;
#else
         // Single-quote the path so spaces and shell metacharacters
         // in the log file path don't break or inject into the command.
         auto shell_quote = [](const std::string& s) {
           std::string out = "'";
           for (char ch : s) {
             if (ch == '\'') {
               out += "'\"'\"'";
             } else {
               out += ch;
             }
           }
           out += "'";
           return out;
         };
         std::string cmd = "tail -n " + std::to_string(lines);
         if (follow)
           cmd += " -f";
         cmd += " ";
         cmd += shell_quote(log_file.string());
         return std::system(cmd.c_str());
#endif
       }});

  // --- plugins command ---
  cli.AddCommand(
      {"plugins",
       "Manage plugins",
       {"p"},
       [logger, gateway_url, auth_token](int argc, char** argv) -> int {
         std::vector<std::string> args;
         for (int i = 1; i < argc; ++i)
           args.push_back(argv[i]);

         std::string sub = args.empty() ? "list" : args[0];
         std::vector<std::string> sub_args;
         if (args.size() > 1)
           sub_args.assign(args.begin() + 1, args.end());

         auto make_client = [&logger, &gateway_url, &auth_token]()
             -> std::shared_ptr<quantclaw::gateway::GatewayClient> {
           auto c = std::make_shared<quantclaw::gateway::GatewayClient>(
               gateway_url, auth_token, logger);
           if (!c->Connect(3000)) {
             std::cerr << "Error: Gateway not running" << '\n';
             return nullptr;
           }
           return c;
         };

         // Validate plugin id: only alphanumeric, hyphens, underscores, dots
         auto validate_plugin_id = [](const std::string& id) -> bool {
           if (id.empty())
             return false;
           for (char c : id) {
             if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' &&
                 c != '_' && c != '.') {
               return false;
             }
           }
           return true;
         };

         if (sub == "list") {
           bool json_output = false;
           for (const auto& a : sub_args) {
             if (a == "--json")
               json_output = true;
           }
           try {
             auto client = make_client();
             if (!client)
               return 1;
             auto result = client->Call("plugins.list", {});
             client->Disconnect();
             if (json_output) {
               std::cout << result.dump(2) << '\n';
               return 0;
             }
             // result is a JSON array of plugin records
             auto arr = result.is_array() ? result
                                          : (result.contains("plugins")
                                                 ? result["plugins"]
                                                 : Json::array());
             if (arr.empty()) {
               std::cout << "No plugins loaded" << '\n';
             } else {
               std::cout << "Plugins (" << arr.size() << "):" << '\n';
               for (const auto& p : arr) {
                 std::string id = p.value("id", "");
                 std::string status = p.value("status", "unknown");
                 bool enabled = p.value("enabled", true);
                 std::string version = p.value("version", "");
                 std::string desc = p.value("description", "");

                 std::cout << "  " << (enabled ? "+" : "-") << " " << id;
                 if (!version.empty())
                   std::cout << " @" << version;
                 std::cout << "  [" << status << "]";
                 if (!desc.empty())
                   std::cout << "  " << desc;
                 std::cout << '\n';
               }
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "status") {
           std::string plugin_id = sub_args.empty() ? "" : sub_args[0];
           try {
             auto client = make_client();
             if (!client)
               return 1;
             auto result = client->Call("plugins.list", {});
             client->Disconnect();
             auto arr = result.is_array() ? result
                                          : (result.contains("plugins")
                                                 ? result["plugins"]
                                                 : Json::array());
             bool found = false;
             for (const auto& p : arr) {
               if (!plugin_id.empty() && p.value("id", "") != plugin_id)
                 continue;
               found = true;
               std::cout << "Plugin: " << p.value("id", "") << '\n';
               if (p.contains("version"))
                 std::cout << "  version:  " << p["version"] << '\n';
               std::cout << "  enabled:  "
                         << (p.value("enabled", true) ? "yes" : "no")
                         << '\n';
               std::cout << "  status:   " << p.value("status", "unknown")
                         << '\n';
               std::cout << "  origin:   " << p.value("origin", "")
                         << '\n';
               if (p.contains("description"))
                 std::cout << "  desc:     " << p["description"] << '\n';
               if (p.contains("tools") && !p["tools"].empty())
                 std::cout << "  tools:    " << p["tools"].dump() << '\n';
               if (p.contains("channels") && !p["channels"].empty())
                 std::cout << "  channels: " << p["channels"].dump()
                           << '\n';
               if (p.contains("hooks") && !p["hooks"].empty())
                 std::cout << "  hooks:    " << p["hooks"].dump() << '\n';
               if (p.contains("error"))
                 std::cout << "  error:    " << p["error"] << '\n';
               std::cout << '\n';
             }
             if (!plugin_id.empty() && !found) {
               std::cerr << "Plugin not found: " << plugin_id << '\n';
               return 1;
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "enable" || sub == "disable") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw plugins " << sub << " <id>"
                       << '\n';
             return 1;
           }
           std::string plugin_id = sub_args[0];
           if (!validate_plugin_id(plugin_id)) {
             std::cerr << "Invalid plugin id: " << plugin_id << '\n';
             return 1;
           }
           bool enable = (sub == "enable");
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             quantclaw::QuantClawConfig::SetValue(
                 config_file, "plugins.entries." + plugin_id + ".enabled",
                 enable);
             std::cout << (enable ? "Enabled" : "Disabled")
                       << " plugin: " << plugin_id << '\n';
             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "install") {
           if (sub_args.empty()) {
             std::cerr
                 << "Usage: quantclaw plugins install <path> [--id <name>]"
                 << '\n';
             return 1;
           }
           std::string plugin_path = sub_args[0];
           std::string plugin_id;
           for (size_t i = 1; i < sub_args.size(); ++i) {
             if (sub_args[i] == "--id" && i + 1 < sub_args.size())
               plugin_id = sub_args[++i];
           }
           // Derive ID from directory name if not provided
           if (plugin_id.empty())
             plugin_id = std::filesystem::path(plugin_path).filename().string();

           if (!validate_plugin_id(plugin_id)) {
             std::cerr << "Invalid plugin id: " << plugin_id << '\n';
             return 1;
           }
           if (!std::filesystem::exists(plugin_path) ||
               !std::filesystem::is_directory(plugin_path)) {
             std::cerr << "Plugin path not found or not a directory: "
                       << plugin_path << '\n';
             return 1;
           }

           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             Json install_entry = Json::object();
             install_entry["installPath"] =
                 std::filesystem::canonical(plugin_path).string();
             quantclaw::QuantClawConfig::SetValue(
                 config_file, "plugins.installs." + plugin_id, install_entry);
             std::cout << "Installed plugin: " << plugin_id << " from "
                       << plugin_path << '\n';
             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         if (sub == "remove") {
           if (sub_args.empty()) {
             std::cerr << "Usage: quantclaw plugins remove <id>" << '\n';
             return 1;
           }
           std::string plugin_id = sub_args[0];
           if (!validate_plugin_id(plugin_id)) {
             std::cerr << "Invalid plugin id: " << plugin_id << '\n';
             return 1;
           }
           try {
             auto config_file = quantclaw::QuantClawConfig::DefaultConfigPath();
             // Remove from installs and entries (ignore if key absent)
             try {
               quantclaw::QuantClawConfig::UnsetValue(
                   config_file, "plugins.installs." + plugin_id);
             } catch (const std::exception& ue) {
               logger->debug("plugins.installs.{} not found: {}", plugin_id,
                             ue.what());
             }
             try {
               quantclaw::QuantClawConfig::UnsetValue(
                   config_file, "plugins.entries." + plugin_id);
             } catch (const std::exception& ue) {
               logger->debug("plugins.entries.{} not found: {}", plugin_id,
                             ue.what());
             }
             std::cout << "Removed plugin: " << plugin_id << '\n';
             auto client = make_client();
             if (client) {
               client->Call("config.reload", {});
               client->Disconnect();
             }
             return 0;
           } catch (const std::exception& e) {
             std::cerr << "Error: " << e.what() << '\n';
             return 1;
           }
         }

         std::cerr << "Unknown plugins subcommand: " << sub << '\n';
         std::cerr
             << "Available: list, status, enable, disable, install, remove"
             << '\n';
         return 1;
       }});

  // --- run command ---
  // One-shot: send a message to the agent and print the response.
  // Equivalent to `quantclaw agent -m "<message>"` but with a simpler
  // interface.
  cli.AddCommand({"run",
                  "Send a one-shot message to the agent and print the response",
                  {},
                  [agent_cmds](int argc, char** argv) -> int {
                    // Collect args; reconstruct as: agent -m "<message>"
                    std::vector<std::string> args;
                    std::string message;
                    bool next_is_session = false;
                    std::string session_key;

                    for (int i = 1; i < argc; ++i) {
                      std::string arg = argv[i];
                      if (next_is_session) {
                        session_key = arg;
                        next_is_session = false;
                      } else if (arg == "-s" || arg == "--session") {
                        next_is_session = true;
                      } else {
                        if (!message.empty())
                          message += " ";
                        message += arg;
                      }
                    }

                    if (message.empty()) {
                      std::cerr
                          << "Usage: quantclaw run <message> [-s <session>]"
                          << '\n';
                      return 1;
                    }

                    args.push_back("-m");
                    args.push_back(message);
                    if (!session_key.empty()) {
                      args.push_back("--session");
                      args.push_back(session_key);
                    }
                    return agent_cmds->RequestCommand(args);
                  }});

  // --- eval command ---
  // Evaluate a prompt directly against the configured model and print result.
  // Useful for quick one-off queries without persistent sessions.
  cli.AddCommand(
      {"eval",
       "Evaluate a prompt against the agent (no session persistence)",
       {},
       [agent_cmds](int argc, char** argv) -> int {
         std::string prompt;
         for (int i = 1; i < argc; ++i) {
           if (!prompt.empty())
             prompt += " ";
           prompt += argv[i];
         }
         if (prompt.empty()) {
           std::cerr << "Usage: quantclaw eval <prompt>" << '\n';
           return 1;
         }
         // Use --no-session flag so no history is persisted
         std::vector<std::string> args = {"-m", prompt, "--no-session"};
         return agent_cmds->RequestCommand(args);
       }});

  return cli.Run(argc, argv);
}
