// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/skill/skill_loader_full.hpp"
#include "quantclaw/skill/skill_parse_file.hpp"

namespace quantclaw {

// ---------------------------------------------------------------------------
// SkillInstallDeps: skill dependency installation.
//
// Responsibilities:
//   Installs skill dependencies via npm, go, uv, apt, brew, or download.
//   Supports checking binary availability before install.
//
// Usage:
//   SkillInstallDeps installer(logger);
//
//   // Install a single skill's dependencies
//   installer.InstallSkillDependence(skill_file_data);
//
//   // Install by skill name
//   installer.InstallSkillDependenceByName("weather", metas);
//
//   // Install all skills with dependencies
//   installer.InstallAllSkillDependences(metas);
// ---------------------------------------------------------------------------

class SkillInstallDeps {
 public:
  // Construct with a logger (internally creates the full loader).
  SkillInstallDeps(std::shared_ptr<spdlog::logger> logger);

  // -----------------------------------------------------------------------
  // Skill dependency installation
  // -----------------------------------------------------------------------

  // Install a skill's dependencies. Returns true if all install steps succeed.
  // Supports node (npm), go, uv, apt, brew, and download methods.
  bool InstallSkillDependence(const SkillFullData& skill);

  // Install a skill's dependencies by name. Searches the metadata list
  // for the skill, loads its full data, and runs the install.
  // Returns true on success, false if skill not found or install fails.
  bool InstallSkillDependenceByName(
      const std::string& skill_name,
      const std::vector<SkillMetadata>& metas);

  // Install all skills that have install instructions and whose gating passes.
  // Returns the number of skills successfully installed.
  int InstallAllSkillDependences(const std::vector<SkillMetadata>& metas);

 private:
  // Check if binary exists in PATH (whitelist + native API, no shell)
  bool IsBinaryAvailable(const std::string& binary_name) const;

  // Validate package ref against whitelist to prevent shell injection.
  // Allows [a-zA-Z0-9_-.@/+=<>:] per method-specific policy.
  static bool IsValidPackageRef(const std::string& ref,
                                const std::string& method);

  // Spawn a command via fork+exec (no shell interpretation).
  // Returns the child exit code, or -1 on spawn failure.
  int RunCommand(const std::vector<std::string>& args) const;

  // Reference to the full loader
  std::shared_ptr<SkillLoaderFull> full_loader_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
