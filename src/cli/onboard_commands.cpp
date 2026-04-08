// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/cli/onboard_commands.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "quantclaw/builtin_skills.hpp"
#include "quantclaw/config.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/platform/process.hpp"
#include "quantclaw/platform/service.hpp"

namespace quantclaw::cli {

// Token generation (OpenClaw-compatible: 48-char hex string)
std::string OnboardCommands::GenerateToken() {
  static constexpr char kHexChars[] = "0123456789abcdef";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 15);
  std::string token;
  token.reserve(48);
  for (int i = 0; i < 48; ++i) {
    token += kHexChars[dist(gen)];
  }
  return token;
}

OnboardCommands::OnboardCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {}

int OnboardCommands::OnboardCommand(const std::vector<std::string>& args) {
  bool install_daemon = false;
  bool skip_daemon = false;

  for (const auto& arg : args) {
    if (arg == "--install-daemon")
      install_daemon = true;
    if (arg == "--skip-daemon")
      skip_daemon = true;
  }

  PrintWelcome();

  // Step 1: Config
  PrintStep(1, 5, "Configuration");
  if (SetupConfig() != 0) {
    std::cerr << "Configuration setup failed" << std::endl;
    return 1;
  }

  // Load port for later steps
  int port = 18800;
  try {
    auto cfg =
        QuantClawConfig::LoadFromFile(QuantClawConfig::DefaultConfigPath());
    if (cfg.gateway.port > 0)
      port = cfg.gateway.port;
  } catch (...) {}

  // Step 2: Workspace
  PrintStep(2, 5, "Workspace Setup");
  if (SetupWorkspace() != 0) {
    std::cerr << "Workspace setup failed" << std::endl;
    return 1;
  }

  // Step 3: Daemon
  PrintStep(3, 5, "Daemon Setup");
  if (!skip_daemon) {
    if (install_daemon ||
        PromptYesNo("Install QuantClaw as a background service?", true)) {
      if (SetupDaemon() != 0) {
        logger_->warn("Daemon setup failed, continuing");
      }
    }
  }

  // Step 4: Skills
  PrintStep(4, 5, "Skills Setup");
  if (SetupSkills() != 0) {
    logger_->warn("Skills setup had issues, but continuing");
  }

  // Step 5: Verification
  PrintStep(5, 5, "Verification");
  if (VerifySetup() != 0) {
    logger_->warn("Some verification checks failed");
  }

  std::cout << "\n✓ Onboarding complete!" << std::endl;
  std::cout << "\nNext steps:" << std::endl;
  std::cout << "  1. Start the gateway:  quantclaw gateway start" << std::endl;
  std::cout << "  2. Check status:       quantclaw status" << std::endl;
  std::cout << "  3. Send a message:     quantclaw agent -m \"Hello\""
            << std::endl;
  std::cout << "  4. Open the dashboard: http://127.0.0.1:" << port + 1
            << std::endl;
  std::cout << "\nFor help: quantclaw --help" << std::endl;

  return 0;
}

int OnboardCommands::InstallDaemonCommand(
    const std::vector<std::string>& /*args*/) {
  // Load or create config first (daemon needs port + token)
  int port = 18800;
  try {
    auto cfg =
        QuantClawConfig::LoadFromFile(QuantClawConfig::DefaultConfigPath());
    if (cfg.gateway.port > 0)
      port = cfg.gateway.port;
  } catch (...) {
    // No config yet — run quick setup so config exists
    std::cout << "No config found. Running quick setup first..." << std::endl;
    if (QuickSetupCommand({}) != 0)
      return 1;
  }

  std::cout << "Installing QuantClaw background service..." << std::endl;
  if (InstallDaemon(port)) {
    std::cout << "✓ Daemon installed successfully" << std::endl;
    std::cout << "\nManage the service:" << std::endl;
    std::cout << "  quantclaw gateway start" << std::endl;
    std::cout << "  quantclaw gateway stop" << std::endl;
    std::cout << "  quantclaw gateway status" << std::endl;
    return 0;
  }
  std::cerr << "✗ Failed to install daemon" << std::endl;
  return 1;
}

// Performs a non-interactive quick setup using sensible defaults.
// Creates the workspace directory, config file, all standard workspace files
// (SOUL, MEMORY, SKILL, IDENTITY, HEARTBEAT, USER, AGENTS, TOOLS), and
// installs built-in skills. Skill installation failures are non-fatal and
// logged as warnings so that the overall setup can still complete.
// Returns 0 on success, 1 on fatal error.
int OnboardCommands::QuickSetupCommand(
    const std::vector<std::string>& /*args*/) {
  std::cout << "Running quick setup..." << std::endl;

  // Token
  std::string token = GenerateToken();

  if (!CreateWorkspaceDirectory()) {
    std::cerr << "Failed to create workspace" << std::endl;
    return 1;
  }
  if (!CreateConfigFile("anthropic/claude-sonnet-4-6", 18800, "127.0.0.1",
                        token)) {
    std::cerr << "Failed to create config" << std::endl;
    return 1;
  }
  if (!CreateSOULFile() || !CreateMemoryFile() || !CreateSkillFile() ||
      !CreateIdentityFile() || !CreateHeartbeatFile() || !CreateUserFile() ||
      !CreateAgentsFile() || !CreateToolsFile()) {
    std::cerr << "Failed to create workspace files" << std::endl;
    return 1;
  }

  if (SetupSkills() != 0) {
    logger_->warn("Skills setup had issues during quick setup, but continuing");
  }

  std::cout << "✓ Quick setup complete" << std::endl;
  return 0;
}

void OnboardCommands::PrintWelcome() {
  std::cout << "\n";
  std::cout << "╔════════════════════════════════════════════════════════════╗"
            << std::endl;
  std::cout << "║                                                            ║"
            << std::endl;
  std::cout << "║          Welcome to QuantClaw Onboarding Wizard            ║"
            << std::endl;
  std::cout << "║                                                            ║"
            << std::endl;
  std::cout << "║  This wizard will guide you through the initial setup of   ║"
            << std::endl;
  std::cout << "║  QuantClaw, including configuration, workspace creation,   ║"
            << std::endl;
  std::cout << "║  and optional daemon installation.                         ║"
            << std::endl;
  std::cout << "║                                                            ║"
            << std::endl;
  std::cout << "╚════════════════════════════════════════════════════════════╝"
            << std::endl;
  std::cout << std::endl;
}

void OnboardCommands::PrintStep(int current, int total,
                                const std::string& title) {
  std::cout << "\n[" << current << "/" << total << "] " << title << std::endl;
  std::cout << std::string(40, '-') << std::endl;
}

std::string OnboardCommands::PromptString(const std::string& prompt,
                                          const std::string& default_value) {
  std::cout << prompt;
  if (!default_value.empty()) {
    std::cout << " [" << default_value << "]";
  }
  std::cout << ": ";
  std::cout.flush();

  std::string input;
  std::getline(std::cin, input);

  if (input.empty() && !default_value.empty()) {
    return default_value;
  }
  return input;
}

bool OnboardCommands::PromptYesNo(const std::string& prompt,
                                  bool default_value) {
  std::string default_str = default_value ? "Y/n" : "y/N";
  std::cout << prompt << " [" << default_str << "]: ";
  std::cout.flush();

  std::string input;
  std::getline(std::cin, input);

  if (input.empty()) {
    return default_value;
  }

  return input[0] == 'y' || input[0] == 'Y';
}

std::string
OnboardCommands::PromptChoice(const std::string& prompt,
                              const std::vector<std::string>& choices) {
  std::cout << prompt << std::endl;
  for (size_t i = 0; i < choices.size(); ++i) {
    std::cout << "  " << (i + 1) << ") " << choices[i] << std::endl;
  }
  std::cout << "Choose [1]: ";
  std::cout.flush();

  std::string input;
  std::getline(std::cin, input);

  int choice = 1;
  if (!input.empty()) {
    try {
      choice = std::stoi(input);
    } catch (...) {
      choice = 1;
    }
  }

  if (choice < 1 || choice > static_cast<int>(choices.size())) {
    choice = 1;
  }

  return choices[static_cast<size_t>(choice - 1)];
}

int OnboardCommands::SetupConfig() {
  std::string config_path = QuantClawConfig::DefaultConfigPath();

  if (std::filesystem::exists(config_path)) {
    std::cout << "Config file already exists at: " << config_path << std::endl;
    if (!PromptYesNo("Overwrite?", false)) {
      return 0;
    }
  }

  std::cout << "\nLet's configure QuantClaw:" << std::endl;

  std::string model =
      PromptString("Default AI model", "anthropic/claude-sonnet-4-6");
  std::string port_str =
      PromptString("Gateway port", std::to_string(kDefaultGatewayPort));
  std::string bind = PromptString("Gateway bind address", "127.0.0.1");

  int port = 18800;
  try {
    port = std::stoi(port_str);
  } catch (...) {}

  // Auto-generate auth token (OpenClaw-compatible behaviour)
  std::string token = GenerateToken();
  std::cout << "  Auto-generated gateway auth token." << std::endl;

  if (!CreateConfigFile(model, port, bind, token)) {
    return 1;
  }

  std::cout << "✓ Configuration saved to: " << config_path << std::endl;
  return 0;
}

int OnboardCommands::SetupWorkspace() {
  if (!CreateWorkspaceDirectory()) {
    return 1;
  }
  if (!CreateSOULFile())
    return 1;
  if (!CreateMemoryFile())
    return 1;
  if (!CreateSkillFile())
    return 1;
  if (!CreateIdentityFile())
    return 1;
  if (!CreateHeartbeatFile())
    return 1;
  if (!CreateUserFile())
    return 1;
  if (!CreateAgentsFile())
    return 1;
  if (!CreateToolsFile())
    return 1;

  std::cout << "✓ Workspace created successfully" << std::endl;
  return 0;
}

int OnboardCommands::SetupDaemon() {
  int port = 18800;
  try {
    auto cfg =
        QuantClawConfig::LoadFromFile(QuantClawConfig::DefaultConfigPath());
    if (cfg.gateway.port > 0)
      port = cfg.gateway.port;
  } catch (...) {}

  std::cout << "\nSetting up QuantClaw as a background service..." << std::endl;

  if (InstallDaemon(port)) {
    std::cout << "✓ Daemon installed successfully" << std::endl;
    std::cout << "\nManage the service:" << std::endl;
    std::cout << "  quantclaw gateway start" << std::endl;
    std::cout << "  quantclaw gateway stop" << std::endl;
    std::cout << "  quantclaw gateway status" << std::endl;
    return 0;
  }
  std::cerr << "✗ Failed to install daemon" << std::endl;
  std::cerr << "You can still run QuantClaw manually: quantclaw gateway"
            << std::endl;
  return 1;
}

// Installs built-in skills into ~/.quantclaw/skills/.
// Each skill is represented by a subdirectory containing a SKILL.md manifest.
// Already-installed skills (directory + SKILL.md exist) are skipped silently.
// Returns 0 if at least one skill was installed or all were already present,
// 1 if every skill installation attempt failed.
int OnboardCommands::SetupSkills() {
  std::string home_str = quantclaw::platform::home_directory();
  auto skills_dir = std::filesystem::path(home_str) / ".quantclaw" / "skills";

  try {
    std::filesystem::create_directories(skills_dir);
  } catch (const std::exception& e) {
    logger_->warn("Failed to create skills directory: {}", e.what());
    return 1;
  }

  int installed = 0;
  int skipped = 0;
  int failed = 0;
  for (const auto& skill : GetBuiltinSkills()) {
    auto skill_dir = skills_dir / skill.name;
    auto skill_file = skill_dir / "SKILL.md";

    if (std::filesystem::exists(skill_file)) {
      ++skipped;
      continue;
    }

    try {
      std::filesystem::create_directories(skill_dir);
      std::ofstream f(skill_file);
      if (!f.is_open()) {
        logger_->warn(
            "Failed to install skill '{}': cannot open file for writing",
            skill.name);
        ++failed;
        continue;
      }
      f << skill.content;
      if (!f) {
        logger_->warn("Failed to install skill '{}': write error", skill.name);
        ++failed;
        continue;
      }
      std::cout << "  + skill: " << skill.name << std::endl;
      ++installed;
    } catch (const std::exception& e) {
      logger_->warn("Failed to install skill '{}': {}", skill.name, e.what());
      ++failed;
    }
  }

  if (installed > 0) {
    std::cout << "✓ Installed " << installed << " built-in skill(s) to "
              << skills_dir.string() << std::endl;
  } else {
    std::cout << "✓ Built-in skills already present (" << skipped << " skipped)"
              << std::endl;
  }
  return failed > 0 ? 1 : 0;
}

int OnboardCommands::VerifySetup() {
  std::cout << "\nVerifying setup..." << std::endl;

  std::string home_str = quantclaw::platform::home_directory();

  // Check config
  std::string config_path = QuantClawConfig::DefaultConfigPath();
  bool config_ok = std::filesystem::exists(config_path);
  std::cout << "  [" << (config_ok ? "✓" : "✗") << "] Config file" << std::endl;

  // Check workspace
  auto workspace =
      std::filesystem::path(home_str) / ".quantclaw/agents/main/workspace";
  bool ws_ok = std::filesystem::exists(workspace);
  std::cout << "  [" << (ws_ok ? "✓" : "✗") << "] Workspace directory"
            << std::endl;

  // Check SOUL.md
  bool soul_ok = std::filesystem::exists(workspace / "SOUL.md");
  std::cout << "  [" << (soul_ok ? "✓" : "✗") << "] SOUL.md" << std::endl;

  // Check AGENTS.md
  bool agents_ok = std::filesystem::exists(workspace / "AGENTS.md");
  std::cout << "  [" << (agents_ok ? "✓" : "✗") << "] AGENTS.md" << std::endl;

  // Check MEMORY.md
  bool memory_ok = std::filesystem::exists(workspace / "MEMORY.md");
  std::cout << "  [" << (memory_ok ? "✓" : "✗") << "] MEMORY.md" << std::endl;

  // Check IDENTITY.md
  bool identity_ok = std::filesystem::exists(workspace / "IDENTITY.md");
  std::cout << "  [" << (identity_ok ? "✓" : "✗") << "] IDENTITY.md"
            << std::endl;

  // Check SKILL.md
  bool skill_ok = std::filesystem::exists(workspace / "SKILL.md");
  std::cout << "  [" << (skill_ok ? "✓" : "✗") << "] SKILL.md" << std::endl;

  // Check HEARTBEAT.md
  bool heartbeat_ok = std::filesystem::exists(workspace / "HEARTBEAT.md");
  std::cout << "  [" << (heartbeat_ok ? "✓" : "✗") << "] HEARTBEAT.md"
            << std::endl;

  // Check USER.md
  bool user_ok = std::filesystem::exists(workspace / "USER.md");
  std::cout << "  [" << (user_ok ? "✓" : "✗") << "] USER.md" << std::endl;

  // Try gateway connection
  int port = 18800;
  try {
    auto cfg = QuantClawConfig::LoadFromFile(config_path);
    if (cfg.gateway.port > 0)
      port = cfg.gateway.port;
  } catch (...) {}

  bool gw_ok = TestGatewayConnection(port);
  std::cout << "  [" << (gw_ok ? "✓" : "-") << "] Gateway connection"
            << (gw_ok ? ""
                      : " (not running — start with: quantclaw gateway start)")
            << std::endl;

  return (config_ok && ws_ok && soul_ok && agents_ok && memory_ok &&
          identity_ok && skill_ok && heartbeat_ok && user_ok)
             ? 0
             : 1;
}

bool OnboardCommands::CreateWorkspaceDirectory() {
  std::string home_str = quantclaw::platform::home_directory();

  try {
    // QuantClaw agent ID: main
    auto workspace =
        std::filesystem::path(home_str) / ".quantclaw/agents/main/workspace";
    std::filesystem::create_directories(workspace);

    // Create standard subdirectories
    std::filesystem::create_directories(workspace / "skills");
    std::filesystem::create_directories(workspace / "scripts");
    std::filesystem::create_directories(workspace / "references");

    // Sessions directory
    auto sessions =
        std::filesystem::path(home_str) / ".quantclaw/agents/main/sessions";
    std::filesystem::create_directories(sessions);

    // Logs directory
    std::filesystem::create_directories(std::filesystem::path(home_str) /
                                        ".quantclaw/logs");

    return true;
  } catch (const std::exception& e) {
    logger_->error("Failed to create workspace: {}", e.what());
    return false;
  }
}

bool OnboardCommands::CreateConfigFile(const std::string& model, int port,
                                       const std::string& bind,
                                       const std::string& token) {
  std::string config_path = QuantClawConfig::DefaultConfigPath();

  try {
    std::filesystem::create_directories(
        std::filesystem::path(config_path).parent_path());

    nlohmann::json config;

    // If config exists, load it to preserve API keys and other settings
    if (std::filesystem::exists(config_path)) {
      try {
        std::ifstream existing_file(config_path);
        existing_file >> config;
      } catch (const std::exception&) {
        // If loading fails, start fresh
        config = nlohmann::json::object();
      }
    }

    // Merge new settings into existing config (preserving API keys)
    config["gateway"]["port"] = port;
    config["gateway"]["bind"] = bind;
    config["gateway"]["auth"]["mode"] = "token";
    config["gateway"]["auth"]["token"] = token;

    // Set defaults for agent and other sections
    if (!config.contains("agent") || config["agent"].is_null()) {
      config["agent"] = {{"model", model},
                         {"autoCompact", true},
                         {"compactMaxMessages", 100},
                         {"maxIterations", 15},
                         {"temperature", 0.7},
                         {"maxTokens", 8192}};
    } else {
      // Update model while preserving other agent settings
      config["agent"]["model"] = model;
    }

    // Ensure models section exists but preserve any existing API keys
    if (!config.contains("models") || config["models"].is_null()) {
      config["models"] = {
          {"defaultModel", model},
          {"providers",
           {{"anthropic", {{"apiKey", ""}}}, {"openai", {{"apiKey", ""}}}}}};
    } else {
      config["models"]["defaultModel"] = model;
      // Preserve existing API keys
      if (!config["models"].contains("providers") ||
          config["models"]["providers"].is_null()) {
        config["models"]["providers"] = {{"anthropic", {{"apiKey", ""}}},
                                         {"openai", {{"apiKey", ""}}}};
      }
    }

    // Set defaults for other sections if not present
    if (!config.contains("queue") || config["queue"].is_null()) {
      config["queue"] = {{"maxConcurrent", 4}, {"debounceMs", 1000}};
    }
    if (!config.contains("session") || config["session"].is_null()) {
      config["session"] = {{"dmScope", "per-channel-peer"}};
    }
    if (!config.contains("channels") || config["channels"].is_null()) {
      config["channels"] = {{"discord", {{"enabled", false}}},
                            {"telegram", {{"enabled", false}}},
                            {"qq", {{"enabled", false}}}};
    }
    if (!config.contains("tools") || config["tools"].is_null()) {
      config["tools"] = {{"allow", nlohmann::json::array()},
                         {"exec", {{"ask", "on-miss"}}}};
    }
    if (!config.contains("mcp") || config["mcp"].is_null()) {
      config["mcp"] = {{"servers", nlohmann::json::array()}};
    }

    std::ofstream file(config_path);
    file << config.dump(2);
    file.close();

    return true;
  } catch (const std::exception& e) {
    logger_->error("Failed to create config: {}", e.what());
    return false;
  }
}

bool OnboardCommands::CreateWorkspaceFile(const std::string& filename,
                                          const std::string& content) {
  std::string home_str = quantclaw::platform::home_directory();
  auto path = std::filesystem::path(home_str) /
              ".quantclaw/agents/main/workspace" / filename;

  try {
    std::ofstream file(path);
    file << content;
    file.close();
    return true;
  } catch (const std::exception& e) {
    logger_->error("Failed to create {}: {}", filename, e.what());
    return false;
  }
}

bool OnboardCommands::CreateSOULFile() {
  return CreateWorkspaceFile(
      "SOUL.md",
      "# QuantClaw Agent Identity\n"
      "\n"
      "## Role\n"
      "You are a helpful AI assistant powered by QuantClaw.\n"
      "\n"
      "## Capabilities\n"
      "- Answer questions and provide information\n"
      "- Help with coding and technical tasks\n"
      "- Assist with analysis and problem-solving\n"
      "- Execute commands and manage files\n"
      "\n"
      "## Constraints\n"
      "- Be honest about your limitations\n"
      "- Respect user privacy\n"
      "- Follow ethical guidelines\n"
      "- Ask for confirmation before destructive operations\n");
}

bool OnboardCommands::CreateMemoryFile() {
  return CreateWorkspaceFile(
      "MEMORY.md",
      "# Memory\n"
      "\n"
      "This file is used to store persistent memory across conversations.\n"
      "The agent will read and update this file to remember important "
      "information.\n"
      "\n"
      "## Key Facts\n"
      "<!-- The agent will add important facts and context here -->\n"
      "\n"
      "## Preferences\n"
      "<!-- User preferences and working style -->\n");
}

bool OnboardCommands::CreateSkillFile() {
  return CreateWorkspaceFile(
      "SKILL.md",
      "# Skills\n"
      "\n"
      "This file documents the agent's available skills and capabilities.\n"
      "\n"
      "## Built-in Skills\n"
      "- **code**: Write and review code\n"
      "- **search**: Search the web and knowledge bases\n"
      "- **files**: Read, write, and manage files\n"
      "- **exec**: Execute shell commands\n"
      "\n"
      "## Custom Skills\n"
      "<!-- Add custom skills installed via: quantclaw skills install <name> "
      "-->\n");
}

bool OnboardCommands::CreateIdentityFile() {
  return CreateWorkspaceFile(
      "IDENTITY.md",
      "# Identity\n"
      "\n"
      "## Agent Name\n"
      "QuantClaw Assistant\n"
      "\n"
      "## Persona\n"
      "A helpful, capable, and honest AI assistant.\n"
      "\n"
      "## Communication Style\n"
      "- Clear and concise\n"
      "- Proactive in asking clarifying questions\n"
      "- Transparent about capabilities and limitations\n"
      "\n"
      "## Values\n"
      "- Accuracy over speed\n"
      "- User safety and privacy\n"
      "- Helpful collaboration\n");
}

bool OnboardCommands::CreateHeartbeatFile() {
  return CreateWorkspaceFile(
      "HEARTBEAT.md",
      "# Heartbeat\n"
      "\n"
      "This file is updated by the agent to indicate it is alive and active.\n"
      "Monitoring systems can check this file to verify agent health.\n"
      "\n"
      "## Status\n"
      "initialized\n"
      "\n"
      "## Last Active\n"
      "<!-- Updated automatically by the agent -->\n");
}

bool OnboardCommands::CreateUserFile() {
  return CreateWorkspaceFile(
      "USER.md",
      "# User Profile\n"
      "\n"
      "This file stores information about the user to personalize "
      "interactions.\n"
      "\n"
      "## Preferences\n"
      "<!-- The agent will learn and record your preferences here -->\n"
      "\n"
      "## Context\n"
      "<!-- Your working context, projects, and goals -->\n"
      "\n"
      "## Notes\n"
      "<!-- Any additional notes about how you like to work -->\n");
}

bool OnboardCommands::CreateAgentsFile() {
  return CreateWorkspaceFile(
      "AGENTS.md",
      "# Agent Configuration\n"
      "\n"
      "## Default Agent\n"
      "The default agent handles general-purpose tasks.\n"
      "\n"
      "## Custom Agents\n"
      "You can define additional agents by creating sub-directories\n"
      "under the workspace, each with its own SOUL.md.\n");
}

bool OnboardCommands::CreateToolsFile() {
  return CreateWorkspaceFile(
      "TOOLS.md",
      "# Tool Configuration\n"
      "\n"
      "## Built-in Tools\n"
      "QuantClaw includes several built-in tools:\n"
      "- **exec**: Execute shell commands\n"
      "- **read_file / write_file**: File operations\n"
      "- **browser**: Web browsing and fetching\n"
      "\n"
      "## MCP Tools\n"
      "Add MCP servers in quantclaw.json to extend tool capabilities.\n");
}

bool OnboardCommands::InstallDaemon(int port) {
  platform::ServiceManager service(logger_);
  int ret = service.install(port);
  return ret == 0;
}

bool OnboardCommands::TestGatewayConnection(int port) {
  try {
    std::string url = "ws://127.0.0.1:" + std::to_string(port);
    auto client = std::make_shared<gateway::GatewayClient>(url, "", logger_);
    if (client->Connect(3000)) {
      client->Disconnect();
      return true;
    }
  } catch (const std::exception&) {}
  return false;
}

}  // namespace quantclaw::cli
