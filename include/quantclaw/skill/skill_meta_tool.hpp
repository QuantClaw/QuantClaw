// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/skill/skill_parse_file.hpp"

namespace quantclaw {
// Skill meta data is described in SKILL.md frontmatter
// Result of a skill invocation handled by the Skill meta-tool.
// Implements dual-context injection (conversation + execution context).
struct SkillInvocationResult {
  bool success = false;
  std::string skill_name;

  // Visible message shown to the user (e.g. "The 'pdf' skill is loading")
  std::string visible_message;

  // Hidden meta-prompt injected into LLM context (full SKILL.md content)
  // This is marked isMeta=true and not displayed to the user.
  std::string hidden_message;

  // Temporary tool permissions granted by this skill (execution context mod)
  std::vector<std::string> allowed_tools;

  // Model override requested by this skill (execution context mod)
  std::string model_override;

  // Error description if success == false
  std::string error;
};

// Schema for the Skill meta-tool registered with the ToolRegistry.
struct SkillMetaToolSchema {
  // Function-calling schema for the "skill" tool
  nlohmann::json parameters;
  // Description string (progressive disclosure: only names + descriptions)
  std::string description;
};

// Skill meta-tool implements the Claude Skills first-principles architecture:
//  - Progressive disclosure: LLM only sees skill names + descriptions
//  - Dual context injection: visible + hidden messages
//  - Execution context modification: temp tool permissions, model switching
class SkillMetaTool {
 public:
  explicit SkillMetaTool(std::shared_ptr<spdlog::logger> logger);

  // Build the meta-tool schema from pre-loaded skill headers.
  // The description only lists skill names and short descriptions;
  // full SKILL.md content is NOT included (progressive disclosure).
  SkillMetaToolSchema BuildSchema(
      const std::vector<SkillMetadata>& meta_datas) const;

  // Handle a skill invocation request from the LLM.
  // |arguments| is the JSON args from the tool call (e.g. {"command":"pdf"})
  // |available_meta_datas| is the list of loaded skill metadata.
  // Dynamically loads full skill data when a skill is invoked.
  SkillInvocationResult HandleInvocation(
      const nlohmann::json& arguments,
      const std::vector<SkillMetadata>& available_meta_datas) const;

 private:
  // Build the visible (user-facing) loading message.
  std::string BuildVisibleMessage(const std::string& skill_name,
                                  const std::string& emoji) const;

  // Build the hidden (AI-facing) meta-prompt from SKILL.md content.
  std::string BuildHiddenMessage(const SkillFullData& skill) const;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
