// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/skill/skill_loader_full.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "quantclaw/skill/skill_parse_file.hpp"
#include "quantclaw/platform/process.hpp"

namespace quantclaw {

SkillLoaderFull::SkillLoaderFull(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
  logger_->info("SkillLoaderFull initialized");
}

std::vector<SkillFullData>
SkillLoaderFull::LoadSkillsFromDirectory(const std::filesystem::path& skills_dir) const {
  std::vector<SkillFullData> skills;

  if (!std::filesystem::exists(skills_dir)) {
    logger_->debug("Skills directory does not exist: {}", skills_dir.string());
    return skills;
  }

  logger_->info("Loading skills from: {}", skills_dir.string());

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(skills_dir)) {
    if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
      try {
        SkillParser parser;
        auto skill = parser.ParseSkillFile(entry.path().string());
        if (CheckSkillGating(skill)) {
          logger_->debug("Loaded skill: {}", skill.mini.name);
          skills.push_back(std::move(skill));
        } else {
          logger_->debug("Skipped skill (gating failed): {}", skill.mini.name);
        }
      } catch (const std::exception& e) {
        logger_->error("Failed to load skill from {}: {}",
                       entry.path().string(), e.what());
      }
    }
  }

  return skills;
}

bool SkillLoaderFull::CheckSkillGating(const SkillFullData& skill) const {
  // If always=true, skip all gating
  if (skill.mini.always) {
    return true;
  }

  // Check OS restriction
  if (!skill.mini.os_restrict.empty()) {
    if (!CheckOsRestriction(skill.mini.os_restrict)) {
      logger_->debug("Skill gating failed: OS not in allowed list");
      return false;
    }
  }

  // Check required binaries (all must exist)
  for (const auto& binary : skill.extra.required_bins) {
    if (!IsBinaryAvailable(binary)) {
      logger_->debug("Skill gating failed: binary '{}' not available", binary);
      return false;
    }
  }

  // Check anyBins (at least one must exist)
  if (!skill.extra.any_bins.empty()) {
    bool found = false;
    for (const auto& binary : skill.extra.any_bins) {
      if (IsBinaryAvailable(binary)) {
        found = true;
        break;
      }
    }
    if (!found) {
      logger_->debug("Skill gating failed: none of anyBins available");
      return false;
    }
  }

  // Check required environment variables
  for (const auto& env_var : skill.extra.required_envs) {
    if (!IsEnvVarAvailable(env_var)) {
      logger_->debug("Skill gating failed: env '{}' not available", env_var);
      return false;
    }
  }

  // Check required config files
  for (const auto& config_file : skill.extra.config_files) {
    std::string expanded = config_file;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
      expanded = platform::home_directory() + expanded.substr(1);
    }
    if (!std::filesystem::exists(expanded)) {
      logger_->debug("Skill gating failed: config '{}' not found", config_file);
      return false;
    }
  }

  return true;
}

std::string
SkillLoaderFull::GetSkillContext(const std::vector<SkillFullData>& full_datas) const {
  std::ostringstream context;

  for (const auto& skill : full_datas) {
    if (!skill.mini.emoji.empty()) {
      context << skill.mini.emoji << " ";
    }
    context << "### " << skill.mini.name << "\n";
    if (!skill.mini.description.empty()) {
      context << skill.mini.description << "\n\n";
    }
    context << skill.body.content << "\n";

    // Append resource directory info if available
    bool has_resources = false;
    if (!skill.mini.scripts_dir.empty() || !skill.mini.references_dir.empty() ||
        !skill.mini.assets_dir.empty()) {
      context << "\n**Resources:**\n";
      has_resources = true;
    }
    if (!skill.mini.scripts_dir.empty()) {
      context << "- Scripts: `" << skill.mini.scripts_dir << "`\n";
    }
    if (!skill.mini.references_dir.empty()) {
      context << "- References: `" << skill.mini.references_dir << "`\n";
    }
    if (!skill.mini.assets_dir.empty()) {
      context << "- Assets: `" << skill.mini.assets_dir << "`\n";
    }

    // List slash commands
    if (!skill.extra.commands.empty()) {
      if (!has_resources)
        context << "\n";
      context << "**Commands:**\n";
      for (const auto& cmd : skill.extra.commands) {
        context << "- `/" << cmd.name << "`";
        if (!cmd.description.empty()) {
          context << " — " << cmd.description;
        }
        context << "\n";
      }
    }

    context << "\n";
  }

  return context.str();
}

std::vector<SkillCommand>
SkillLoaderFull::GetAllCommands(const std::vector<SkillFullData>& full_datas) const {
  std::vector<SkillCommand> commands;
  for (const auto& skill : full_datas) {
    for (const auto& cmd : skill.extra.commands) {
      commands.push_back(cmd);
    }
  }
  return commands;
}

bool SkillLoaderFull::IsBinaryAvailable(const std::string& binary_name) const {
  // Whitelist validation: only allow [a-zA-Z0-9_-] in binary names.
  // Prevents shell injection via metacharacters in skill config files.
  if (binary_name.empty() || binary_name.size() > 255) return false;
  for (char c : binary_name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' &&
        c != '_') {
      return false;
    }
  }

#ifdef _WIN32
  // Windows: search PATH directories for the binary (no shell invocation).
  const char* path_env = std::getenv("PATH");
  if (!path_env) return false;
  std::istringstream iss(path_env);
  std::string dir;
  while (std::getline(iss, dir, ';')) {
    if (dir.empty()) continue;
    auto candidate = std::filesystem::path(dir) / (binary_name + ".exe");
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) return true;
    // Also check without .exe (e.g. batch scripts)
    auto candidate_no_ext = std::filesystem::path(dir) / binary_name;
    if (std::filesystem::exists(candidate_no_ext, ec) && !ec) return true;
  }
  return false;
#else
  // POSIX: search PATH directories using access(X_OK) (no shell invocation).
  const char* path_env = std::getenv("PATH");
  if (!path_env) return false;
  std::istringstream iss(path_env);
  std::string dir;
  while (std::getline(iss, dir, ':')) {
    if (dir.empty()) continue;
    auto full_path = std::filesystem::path(dir) / binary_name;
    if (access(full_path.c_str(), X_OK) == 0) return true;
  }
  return false;
#endif
}

bool SkillLoaderFull::IsEnvVarAvailable(const std::string& env_var) const {
  const char* value = std::getenv(env_var.c_str());
  return value != nullptr && std::strlen(value) > 0;
}

bool SkillLoaderFull::CheckOsRestriction(
    const std::vector<std::string>& os_list) const {
  std::string current = GetCurrentOs();
  for (const auto& os : os_list) {
    std::string normalized = os;
    if (normalized == "macos")
      normalized = "darwin";
    if (normalized == current)
      return true;
  }
  return false;
}

std::string SkillLoaderFull::GetCurrentOs() const {
#ifdef __linux__
  return "linux";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(_WIN32)
  return "win32";
#else
  return "unknown";
#endif
}

std::vector<SkillFullData>
SkillLoaderFull::LoaderMultipleFullData(const SkillsConfig& skills_config,
                            const std::filesystem::path& workspace_path) const {
  // Build ordered directory list: workspace > user > extraDirs
  std::vector<std::filesystem::path> dirs;
  dirs.push_back(workspace_path / "skills");

  dirs.push_back(std::filesystem::path(platform::home_directory()) /
                 ".quantclaw" / "skills");

  for (const auto& extra : skills_config.load.extra_dirs) {
    dirs.push_back(std::filesystem::path(extra));
  }

  // Load from each directory, dedup by name (first wins)
  std::unordered_set<std::string> seen_names;
  std::vector<SkillFullData> result;

  for (const auto& dir : dirs) {
    auto skills = LoadSkillsFromDirectory(dir);
    for (auto& skill : skills) {
      if (seen_names.count(skill.mini.name))
        continue;

      // Check per-skill disable
      auto it = skills_config.entries.find(skill.mini.name);
      if (it != skills_config.entries.end() && !it->second.enabled) {
        logger_->debug("Skill '{}' disabled via config", skill.mini.name);
        continue;
      }

      seen_names.insert(skill.mini.name);
      result.push_back(std::move(skill));
    }
  }

  logger_->info("Loaded {} skills from {} directories", result.size(),
                dirs.size());
  return result;
}

// ---------------------------------------------------------------------------
// Full loaders
// ---------------------------------------------------------------------------

SkillFullData
SkillLoaderFull::LoaderOneFullData(const std::string& skill_file) const {
  // Direct return of prvalue — guaranteed copy elision (C++17).
  SkillParser parser;
  return parser.ParseSkillFile(skill_file, /*load_content=*/true);
}

std::vector<SkillFullData>
SkillLoaderFull::LoaderMultipleFullData(
    const std::vector<std::string>& skill_files) const {
  std::vector<SkillFullData> result;
  result.reserve(skill_files.size());
  for (const auto& path : skill_files) {
    try {
      result.push_back(LoaderOneFullData(path));
    } catch (const std::exception& e) {
      logger_->error("Failed to load skill file '{}': {}", path, e.what());
    }
  }
  return result;
}

SkillFullData
SkillLoaderFull::LoaderOneFullData(const SkillMetadata& metadata) const {
  // Re-parse the SKILL.md with content loading enabled.
  // The root_dir points to the skill directory; SKILL.md is inside it.
  std::string skill_file =
      (std::filesystem::path(metadata.mini.root_dir) / "SKILL.md").string();
  // Direct return of prvalue — guaranteed copy elision (C++17).
  SkillParser parser;
  return parser.ParseSkillFile(skill_file, /*load_content=*/true);
}

std::vector<SkillFullData>
SkillLoaderFull::LoaderMultipleFullData(
    const std::vector<SkillMetadata>& meta_datas) const {
  std::vector<SkillFullData> result;
  result.reserve(meta_datas.size());
  for (const auto& meta : meta_datas) {
    try {
      result.push_back(LoaderOneFullData(meta));
    } catch (const std::exception& e) {
      logger_->error("Failed to load skill '{}' from metadata: {}",
                     meta.mini.name, e.what());
    }
  }
  return result;
}

}  // namespace quantclaw
