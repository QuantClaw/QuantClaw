// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"

namespace quantclaw {

// Install method for skill auto-install.
// Supports both QuantClaw flat format (method/formula/binary) and
// OpenClaw array format (kind/formula/package/bins).
struct SkillInstallInfo {
  std::string method;   // "node", "go", "uv", "download", "apt", "brew"
  std::string formula;  // package/URL to install
  std::string binary;   // expected binary after install

  // OpenClaw extended fields
  std::string kind;               // alias for method
  std::string id;                 // optional install spec identifier
  std::string label;              // human-readable label
  std::string package;            // npm/go package name
  std::string module;             // node module name
  std::string url;                // download URL
  std::string archive;            // archive type
  std::string target_dir;         // target directory for download
  std::vector<std::string> bins;  // expected binaries (OpenClaw format)
  std::vector<std::string> os;    // OS restriction for this install
  bool extract = false;           // extract archive
  int strip_components = 0;       // tar strip-components

  // Returns the effective install method (kind takes precedence if set)
  std::string EffectiveMethod() const {
    return kind.empty() ? method : kind;
  }

  // Returns the effective package identifier
  std::string EffectiveFormula() const {
    if (!formula.empty())
      return formula;
    if (!package.empty())
      return package;
    if (!module.empty())
      return module;
    if (!url.empty())
      return url;
    return "";
  }

  // Returns the first expected binary
  std::string EffectiveBinary() const {
    if (!binary.empty())
      return binary;
    if (!bins.empty())
      return bins.front();
    return "";
  }
};

// Slash command defined in a skill
struct SkillCommand {
  std::string name;  // command name (no leading /)
  std::string description;
  std::string tool_name;  // tool to invoke
  std::string arg_mode;   // "freeform", "json", "none"
};

struct SkillMetadata {
  std::string name;
  std::string description;
  std::vector<std::string> required_bins;
  std::vector<std::string> required_envs;
  std::vector<std::string> any_bins;      // at least one must exist
  std::vector<std::string> config_files;  // required config files
  std::vector<std::string> os_restrict;   // e.g. ["linux", "darwin", "win32"]
  bool always = false;                    // skip all gating
  std::string primary_env;                // primary environment variable
  std::string emoji;                      // display emoji
  std::string homepage;                   // skill homepage URL
  std::string skill_key;                  // alternative skill key
  std::string content;

  // Phase 3 additions
  std::filesystem::path root_dir;          // skill root directory
  std::vector<SkillInstallInfo> installs;  // auto-install instructions
  std::vector<SkillCommand> commands;      // slash commands

  // Resource directories (absolute paths if they exist)
  std::string scripts_dir;
  std::string references_dir;
  std::string assets_dir;
};

class SkillLoader {
 public:
  explicit SkillLoader(std::shared_ptr<spdlog::logger> logger);

  // Load skills from directory (compatible with OpenClaw SKILL.md format)
  std::vector<SkillMetadata>
  LoadSkillsFromDirectory(const std::filesystem::path& skills_dir);

  // Multi-directory loading with dedup and config filtering
  std::vector<SkillMetadata>
  LoadSkills(const SkillsConfig& skills_config,
             const std::filesystem::path& workspace_path);

  // Check if skill can be loaded based on environment (gating)
  bool CheckSkillGating(const SkillMetadata& skill);

  // Get skill content for LLM context (includes resource path info)
  std::string GetSkillContext(const std::vector<SkillMetadata>& skills) const;

  // Install a skill's dependencies (returns true if all succeed)
  bool InstallSkill(const SkillMetadata& skill);

  // Get all slash commands from loaded skills
  std::vector<SkillCommand>
  GetAllCommands(const std::vector<SkillMetadata>& skills) const;

 private:
  // Parse SKILL.md file
  SkillMetadata parse_skill_file(const std::filesystem::path& skill_file) const;

  // Parse YAML frontmatter with indent-aware nesting support
  nlohmann::json parse_yaml_frontmatter(const std::string& yaml_str) const;

  // Check if binary exists in PATH
  bool is_binary_available(const std::string& binary_name) const;

  // Check if environment variable exists
  bool is_env_var_available(const std::string& env_var) const;

  // Check current OS against restriction list
  bool check_os_restriction(const std::vector<std::string>& os_list) const;

  // Get current OS identifier
  std::string get_current_os() const;

  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
