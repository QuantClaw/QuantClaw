// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/skill/skill_loader_meta.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "quantclaw/skill/skill_parse_file.hpp"
#include "quantclaw/platform/process.hpp"

namespace quantclaw {

SkillLoaderMeta::SkillLoaderMeta(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
  logger_->info("SkillLoaderMeta initialized");
}

std::vector<SkillMetadata> SkillLoaderMeta::LoadMetaDataFromDirectory(
    const std::string& skills_dir) const {
  std::vector<SkillMetadata> skills;

  if (!std::filesystem::exists(skills_dir)) {
    logger_->debug("Skills directory does not exist: {}", skills_dir);
    return skills;
  }

  logger_->info("Preloading skill headers from: {}", skills_dir);

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(skills_dir)) {
    if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
      try {
        auto skill = LoaderOneMetaData(entry.path().string());
        logger_->debug("Preloaded skill header: {}", skill.mini.name);
        skills.push_back(std::move(skill));
      } catch (const std::exception& e) {
        logger_->error("Failed to preload skill header from {}: {}",
                       entry.path().string(), e.what());
      }
    }
  }

  return skills;
}

SkillMetadata SkillLoaderMeta::LoaderOneMetaData(
    const std::string& skill_file) const {
  // Delegate to the shared parser; extract metadata only (no content).
  SkillParser parser;
  auto file_data = parser.ParseSkillFile(skill_file, /*load_content=*/false);
  return SkillMetadata{std::move(file_data.mini), std::move(file_data.extra)};
}

std::vector<SkillMetadata> SkillLoaderMeta::LoaderAllMetaData(
    const SkillsConfig& skills_config,
    const std::string& workspace_path) {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  if (skills_scan_complete_) {
    logger_->info("Skills scan complete, returning cached metadata");
    return skills_meta_;
  }
    
  // Build ordered directory list: workspace > user > extraDirs
  std::vector<std::string> dirs;
  dirs.push_back((std::filesystem::path(workspace_path) / "skills").string());
  dirs.push_back((std::filesystem::path(platform::home_directory()) /
                 ".quantclaw" / "skills")
                    .string());

  for (const auto& extra : skills_config.load.extra_dirs) {
    dirs.push_back(extra);
  }

  // Preload from each directory, dedup by name (first wins)
  std::unordered_set<std::string> seen_names;
  skills_meta_.clear();

  for (const auto& dir : dirs) {
    auto skills = LoadMetaDataFromDirectory(dir);
    for (auto& skill : skills) {
      if (seen_names.count(skill.mini.name)) continue;

      // Check per-skill disable
      auto it = skills_config.entries.find(skill.mini.name);
      if (it != skills_config.entries.end() && !it->second.enabled) {
        logger_->debug("Skill '{}' disabled via config", skill.mini.name);
        continue;
      }

      seen_names.insert(skill.mini.name);
      skills_meta_.push_back(std::move(skill));
    }
  }

  logger_->info("Preloaded {} skill headers from {} directories", skills_meta_.size(),
                dirs.size());
  skills_scan_complete_ = true;
  return skills_meta_;
}

bool SkillLoaderMeta::AddOneMetaData(const std::string& skill_file) {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  try {
    // Load metadata from the new skill file
    auto new_meta = LoaderOneMetaData(skill_file);

    // Check for duplicate by skill name
    for (const auto& existing : skills_meta_) {
      if (existing.mini.name == new_meta.mini.name) {
        logger_->debug("AddOneMetaData: skill '{}' already exists, skipping",
                       new_meta.mini.name);
        return false;
      }
    }

    // Add to the list
    skills_meta_.push_back(std::move(new_meta));
    logger_->info("AddOneMetaData: added skill '{}' from {}",
                  skills_meta_.back().mini.name, skill_file);
    return true;
  } catch (const std::exception& e) {
    logger_->error("AddOneMetaData: failed to load metadata from {}: {}",
                   skill_file, e.what());
    return false;
  }
}

bool SkillLoaderMeta::RemoveOneMetaData(const std::string& skill_name) {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  // Find and remove the skill by name
  auto it = std::remove_if(skills_meta_.begin(), skills_meta_.end(),
                           [&skill_name](const SkillMetadata& meta) {
                             return meta.mini.name == skill_name;
                           });

  if (it == skills_meta_.end()) {
    logger_->debug("RemoveOneMetaData: skill '{}' not found", skill_name);
    return false;
  }

  // Erase the removed element(s)
  skills_meta_.erase(it, skills_meta_.end());
  logger_->info("RemoveOneMetaData: removed skill '{}'", skill_name);
  return true;
}

bool SkillLoaderMeta::RewriteOneMetaData(const std::string& skill_file) {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  try {
    // Load fresh metadata from the file
    auto updated_meta = LoaderOneMetaData(skill_file);

    // Find and replace the existing entry by skill name
    for (auto& existing : skills_meta_) {
      if (existing.mini.name == updated_meta.mini.name) {
        const auto name = updated_meta.mini.name;
        existing = std::move(updated_meta);
        logger_->info("RewriteOneMetaData: updated skill '{}' from {}",
                      name, skill_file);
        return true;
      }
    }

    logger_->debug("RewriteOneMetaData: skill '{}' not found in list",
                   updated_meta.mini.name);
    return false;
  } catch (const std::exception& e) {
    logger_->error("RewriteOneMetaData: failed to load metadata from {}: {}",
                   skill_file, e.what());
    return false;
  }
}

std::string SkillLoaderMeta::MergeSkillContext(
    const std::vector<SkillMetadata>& metas) const {
  std::ostringstream context;

  for (const auto& skill : metas) {
    // Add emoji if available
    if (!skill.mini.emoji.empty()) {
      context << skill.mini.emoji << " ";
    }
    
    // Add skill name as heading
    context << "### " << skill.mini.name << "\n";
    
    // Add description if available
    if (!skill.mini.description.empty()) {
      context << skill.mini.description << "\n\n";
    }

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

std::string SkillLoaderMeta::MergeSkillContext() const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  return MergeSkillContext(skills_meta_);
}

std::vector<SkillMetadata> SkillLoaderMeta::GetAllMetaData() const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  return skills_meta_;
}

bool SkillLoaderMeta::IsLoadAllMetaData() const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  return skills_scan_complete_;
}

std::optional<SkillMetadata> SkillLoaderMeta::FindMetaDataByName(
    const std::string& skill_name) const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  for (const auto& meta : skills_meta_) {
    if (meta.mini.name == skill_name) {
      return meta;  // value copy — safe even after lock is released
    }
  }
  logger_->debug("FindMetaDataByName: skill '{}' not found", skill_name);
  return std::nullopt;
}

std::optional<SkillMetadata> SkillLoaderMeta::FindMetaDataByKey(
    const std::string& skill_key) const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  for (const auto& meta : skills_meta_) {
    if (meta.extra.skill_key == skill_key) {
      return meta;  // value copy
    }
  }
  logger_->debug("FindMetaDataByKey: skill_key '{}' not found", skill_key);
  return std::nullopt;
}

std::optional<SkillMetadata> SkillLoaderMeta::FindMetaDataByFile(
    const std::string& skill_file) const {
  std::lock_guard<std::mutex> lock(skills_meta_mutex_);
  // Extract root_dir (parent path) and name (directory name) from file path
  auto file_path = std::filesystem::path(skill_file);
  auto root_dir = file_path.parent_path().string();
  auto dir_name = file_path.parent_path().filename().string();

  for (const auto& meta : skills_meta_) {
    if (meta.mini.root_dir == root_dir && meta.mini.name == dir_name) {
      return meta;  // value copy
    }
  }
  logger_->debug("FindMetaDataByFile: no match for file '{}'", skill_file);
  return std::nullopt;
}

}  // namespace quantclaw
