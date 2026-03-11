// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/prompt_builder.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "quantclaw/config.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw {

PromptBuilder::PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                             std::shared_ptr<SkillLoader> skill_loader,
                             std::shared_ptr<ToolRegistry> tool_registry,
                             const QuantClawConfig* config)
    : memory_manager_(memory_manager),
      skill_loader_(skill_loader),
      tool_registry_(tool_registry),
      config_(config) {}

std::string PromptBuilder::BuildFull(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // 1. SOUL.md - identity
  auto soul = get_section("SOUL.md");
  if (!soul.empty()) {
    prompt << "## Your Identity\n" << soul << "\n\n";
  }

  // 2. AGENTS.md - behavior instructions (OpenClaw)
  auto agents = get_section("AGENTS.md");
  if (!agents.empty()) {
    prompt << "## Agent Behavior\n" << agents << "\n\n";
  }

  // 3. TOOLS.md - tool usage guide (OpenClaw)
  auto tools = get_section("TOOLS.md");
  if (!tools.empty()) {
    prompt << "## Tool Usage Guide\n" << tools << "\n\n";
  }

  // 4. Loaded skills (multi-dir if config available, single-dir fallback)
  std::vector<SkillMetadata> skills;
  if (config_) {
    skills = skill_loader_->LoadSkills(config_->skills,
                                       memory_manager_->GetWorkspacePath());
  } else {
    skills = skill_loader_->LoadSkillsFromDirectory(
        memory_manager_->GetWorkspacePath() / "skills");
  }
  if (!skills.empty()) {
    prompt << "## Available Skills\n"
           << skill_loader_->GetSkillContext(skills) << "\n\n";
  }

  // 5. Memory context (recent daily memory)
  try {
    auto memory_content = get_section("MEMORY.md");
    if (!memory_content.empty()) {
      prompt << "## Memory\n" << memory_content << "\n\n";
    }
  } catch (const std::exception&) {}

  // 6. Runtime info
  prompt << "## Runtime Information\n" << get_runtime_info() << "\n\n";

  // 7. Available tools
  auto tool_schemas = tool_registry_->GetToolSchemas();
  if (!tool_schemas.empty()) {
    prompt << "## Available Tools\n";
    for (const auto& schema : tool_schemas) {
      prompt << "- **" << schema.name << "**: " << schema.description << "\n";
    }
    prompt << "\n";
  }

  // Default identity fallback
  prompt << "You are QuantClaw, a high-performance C++ personal AI assistant. "
         << "Use the available tools when needed to help the user. "
         << "Always be concise and helpful.";

  return prompt.str();
}

std::string PromptBuilder::BuildMinimal(const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // Identity only
  auto soul = get_section("SOUL.md");
  if (!soul.empty()) {
    prompt << "## Your Identity\n" << soul << "\n\n";
  }

  // Tools
  auto tool_schemas = tool_registry_->GetToolSchemas();
  if (!tool_schemas.empty()) {
    prompt << "## Available Tools\n";
    for (const auto& schema : tool_schemas) {
      prompt << "- **" << schema.name << "**: " << schema.description << "\n";
    }
    prompt << "\n";
  }

  prompt << "You are QuantClaw, a helpful AI assistant.";

  return prompt.str();
}

std::string PromptBuilder::get_section(const std::string& filename) const {
  try {
    return memory_manager_->ReadIdentityFile(filename);
  } catch (const std::exception&) {
    return "";
  }
}

std::string PromptBuilder::get_runtime_info() const {
  std::ostringstream info;

  // Current time
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif

  info << "- Current time: " << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ")
       << "\n";
  info << "- Workspace: " << memory_manager_->GetWorkspacePath().string()
       << "\n";
  info << "- Platform: "
#ifdef __linux__
       << "linux"
#elif defined(__APPLE__)
       << "darwin"
#elif defined(_WIN32)
       << "win32"
#else
       << "unknown"
#endif
       << "\n";

  return info.str();
}

}  // namespace quantclaw
