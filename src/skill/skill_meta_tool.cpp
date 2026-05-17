// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/skill/skill_meta_tool.hpp"

#include <sstream>

#include "quantclaw/skill/skill_loader_full.hpp"
#include "quantclaw/skill/skill_loader_meta.hpp"

namespace quantclaw {

SkillMetaTool::SkillMetaTool(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
  logger_->info("SkillMetaTool initialized");
}

SkillMetaToolSchema SkillMetaTool::BuildSchema(
    const std::vector<SkillMetadata>& meta_datas) const {
  SkillMetaToolSchema schema;

  // Progressive disclosure: description only lists names + short descriptions.
  // Full SKILL.md content is hidden until the skill is invoked.
  std::ostringstream desc;
  desc << "Activate a specialized skill to handle the current task. "
          "Available skills:\n";
  for (const auto& skill : meta_datas) {
    if (!skill.mini.emoji.empty()) {
      desc << skill.mini.emoji << " ";
    }
    desc << skill.mini.name;
    if (!skill.mini.description.empty()) {
      desc << " — " << skill.mini.description;
    }
    desc << "\n";
  }
  desc << "\nWhen you need a skill, call this tool with the skill name. "
          "The full skill instructions will be injected into context.";
  schema.description = desc.str();

  // Parameters: single "command" field with the skill name
  schema.parameters = nlohmann::json::parse(
      R"JSON({
        "type": "object",
        "properties": {
          "command": {
            "type": "string",
            "description": "Name of the skill to activate"
          }
        },
        "required": ["command"]
      })JSON");

  return schema;
}

SkillInvocationResult SkillMetaTool::HandleInvocation(
    const nlohmann::json& arguments,
    const std::vector<SkillMetadata>& available_meta_datas) const {
  SkillInvocationResult result;

  logger_->debug("Handling skill invocation with arguments: {}", arguments.dump());
  std::string command = arguments.value("command", "");
  if (command.empty()) {
    result.error = "Missing required parameter: command";
    logger_->warn("Skill invocation failed: no command provided");
    return result;
  }

  // Find the requested skill metadata by name
  const SkillMetadata* target_meta = nullptr;
  for (const auto& skill_meta : available_meta_datas) {
    if (skill_meta.mini.name == command ||
        (!skill_meta.extra.skill_key.empty() && skill_meta.extra.skill_key == command)) {
      target_meta = &skill_meta;
      break;
    }
  }

  if (!target_meta) {
    result.error = "Skill not found: " + command;
    logger_->warn("Skill invocation failed: '{}' not found among {} skills",
                  command, available_meta_datas.size());
    return result;
  }

  // Dynamically load full skill data from metadata
  try {
    SkillLoaderFull skill_loader_full(logger_);
    SkillFullData full_skill = skill_loader_full.LoaderOneFullData(*target_meta);

    result.success = true;
    result.skill_name = full_skill.mini.name;
    result.visible_message = BuildVisibleMessage(full_skill.mini.name, full_skill.mini.emoji);
    result.hidden_message = BuildHiddenMessage(full_skill);
    result.allowed_tools = full_skill.extra.allowed_tools;
    result.model_override = full_skill.extra.model_override;

    logger_->info("Skill '{}' invoked: injecting dual-context (visible={} chars, "
                  "hidden={} chars, allowed_tools={})",
                  full_skill.mini.name, result.visible_message.size(),
                  result.hidden_message.size(), full_skill.extra.allowed_tools.size());
  } catch (const std::exception& e) {
    result.error = "Failed to load skill content: " + std::string(e.what());
    logger_->error("Failed to load full skill data for '{}': {}", 
                   target_meta->mini.name, e.what());
  }

  return result;
}

std::string SkillMetaTool::BuildVisibleMessage(const std::string& skill_name,
                                                const std::string& emoji) const {
  std::ostringstream oss;
  oss << "<command-message>The \"" << skill_name << "\" skill is loading";
  if (!emoji.empty()) {
    oss << " " << emoji;
  }
  oss << "</command-message>";
  return oss.str();
}

std::string SkillMetaTool::BuildHiddenMessage(const SkillFullData& skill) const {
  std::ostringstream oss;
  oss << "You are now operating in the '" << skill.mini.name << "' skill mode.\n\n";
  if (!skill.mini.description.empty()) {
    oss << "Description: " << skill.mini.description << "\n\n";
  }
  oss << skill.body.content << "\n";

  // Append resource directory info if available
  if (!skill.mini.scripts_dir.empty() || !skill.mini.references_dir.empty() ||
      !skill.mini.assets_dir.empty()) {
    oss << "\n**Resources:**\n";
    if (!skill.mini.scripts_dir.empty()) {
      oss << "- Scripts: `" << skill.mini.scripts_dir << "`\n";
    }
    if (!skill.mini.references_dir.empty()) {
      oss << "- References: `" << skill.mini.references_dir << "`\n";
    }
    if (!skill.mini.assets_dir.empty()) {
      oss << "- Assets: `" << skill.mini.assets_dir << "`\n";
    }
  }

  // List slash commands
  if (!skill.extra.commands.empty()) {
    oss << "\n**Commands:**\n";
    for (const auto& cmd : skill.extra.commands) {
      oss << "- ` /" << cmd.name << "`";
      if (!cmd.description.empty()) {
        oss << " — " << cmd.description;
      }
      oss << "\n";
    }
  }
  return oss.str();
}

}  // namespace quantclaw
