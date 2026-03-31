// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.skill_loader;

import std;
import quantclaw.config;
import nlohmann.json;

export namespace quantclaw {

struct SkillInstallInfo {
  std::string method;
  std::string formula;
  std::string binary;
  std::string kind;
  std::string id;
  std::string label;
  std::string package;
  std::string module;
  std::string url;
  std::string archive;
  std::string target_dir;
  std::vector<std::string> bins;
  std::vector<std::string> os;
  bool extract = false;
  int strip_components = 0;

  std::string EffectiveMethod() const {
    return kind.empty() ? method : kind;
  }

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

  std::string EffectiveBinary() const {
    if (!binary.empty())
      return binary;
    if (!bins.empty())
      return bins.front();
    return "";
  }
};

struct SkillCommand {
  std::string name;
  std::string description;
  std::string tool_name;
  std::string arg_mode;
};

struct SkillMetadata {
  std::string name;
  std::string description;
  std::vector<std::string> required_bins;
  std::vector<std::string> required_envs;
  std::vector<std::string> any_bins;
  std::vector<std::string> config_files;
  std::vector<std::string> os_restrict;
  bool always = false;
  std::string primary_env;
  std::string emoji;
  std::string homepage;
  std::string skill_key;
  std::string content;
  std::filesystem::path root_dir;
  std::vector<SkillInstallInfo> installs;
  std::vector<SkillCommand> commands;
  std::string scripts_dir;
  std::string references_dir;
  std::string assets_dir;
};

class SkillLoader {
 public:
  explicit SkillLoader(std::shared_ptr<spdlog::logger> logger);

  std::vector<SkillMetadata>
  LoadSkillsFromDirectory(const std::filesystem::path& skills_dir);

  std::vector<SkillMetadata>
  LoadSkills(const SkillsConfig& skills_config,
             const std::filesystem::path& workspace_path);

  bool CheckSkillGating(const SkillMetadata& skill);
  std::string GetSkillContext(const std::vector<SkillMetadata>& skills) const;
  bool InstallSkill(const SkillMetadata& skill);
  std::vector<SkillCommand>
  GetAllCommands(const std::vector<SkillMetadata>& skills) const;

 private:
  SkillMetadata parse_skill_file(const std::filesystem::path& skill_file) const;
  nlohmann::json parse_yaml_frontmatter(const std::string& yaml_str) const;
  bool is_binary_available(const std::string& binary_name) const;
  bool is_env_var_available(const std::string& env_var) const;
  bool check_os_restriction(const std::vector<std::string>& os_list) const;
  std::string get_current_os() const;

  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw