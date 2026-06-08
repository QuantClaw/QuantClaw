// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/skill/skill_parse_file.hpp"

namespace quantclaw {

// Loads complete SkillFullData (mini + extra + body) from SKILL.md files.
// "Full" indicates that the entire file — YAML frontmatter AND markdown
// body — is parsed and returned, in contrast to SkillLoaderMeta which
// only extracts metadata (frontmatter) without reading the body.
class SkillLoaderFull {
 public:
  explicit SkillLoaderFull(std::shared_ptr<spdlog::logger> logger);

  // Check if skill can be loaded based on environment (gating)
  bool CheckSkillGating(const SkillFullData& skill) const;

  // Get skill content for LLM context (includes resource path info)
  std::string GetSkillContext(const std::vector<SkillFullData>& full_datas) const;

  // Check if binary exists in PATH (public: also used by SkillInstallDeps)
  bool IsBinaryAvailable(const std::string& binary_name) const;

  // Get all slash commands from loaded skills
  std::vector<SkillCommand>
  GetAllCommands(const std::vector<SkillFullData>& full_datas) const;

  // -----------------------------------------------------------------------
  // Full loaders: parse SKILL.md and return complete SkillFullData
  // (mini + extra + body).  All return by value with internal std::move
  // to avoid object copies.
  // -----------------------------------------------------------------------

  // Load a single skill file by file path.
  SkillFullData LoaderOneFullData(const std::string& skill_file) const;

  // Load multiple skill files by file paths.
  std::vector<SkillFullData>
  LoaderMultipleFullData(const std::vector<std::string>& skill_files) const;

  // Load a single skill file from a pre-parsed SkillMetadata (reads body
  // content from the file pointed to by metadata.mini.root_dir).
  SkillFullData LoaderOneFullData(const SkillMetadata& metadata) const;

  // Load multiple skill files from pre-parsed SkillMetadata entries.
  std::vector<SkillFullData>
  LoaderMultipleFullData(const std::vector<SkillMetadata>& meta_datas) const;

  // Multi-directory loading with dedup and config filtering.
  std::vector<SkillFullData>
  LoaderMultipleFullData(const SkillsConfig& skills_config,
                         const std::filesystem::path& workspace_path) const;

 private:
  // Load skills from directory (compatible with OpenClaw SKILL.md format)
  std::vector<SkillFullData>
  LoadSkillsFromDirectory(const std::filesystem::path& skills_dir) const;

  bool IsEnvVarAvailable(const std::string& env_var) const;

  // Check current OS against restriction list
  bool CheckOsRestriction(const std::vector<std::string>& os_list) const;

  // Get current OS identifier
  std::string GetCurrentOs() const;

  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
