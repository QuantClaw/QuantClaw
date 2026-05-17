// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/skill/skill_parse_file.hpp"

namespace quantclaw {

// Loads SkillMetadata (YAML frontmatter only) from SKILL.md files
// without reading the full markdown content (SkillContent is left empty).
// Used at gateway startup to discover available skills quickly.
class SkillLoaderMeta {
 public:
  explicit SkillLoaderMeta(std::shared_ptr<spdlog::logger> logger);

  // Load metadata from a single SKILL.md file.
  // Returns SkillMetadata (mini + extra); body.content is not loaded.
  SkillMetadata LoaderOneMetaData(const std::string& skill_file) const;

  // Multi-directory metadata loading with deduplication by name (first wins)
  // and config-based filtering (respects skills_config.entries[].enabled).
  std::vector<SkillMetadata> LoaderAllMetaData(
      const SkillsConfig& skills_config,
      const std::string& workspace_path);

  // Add metadata from a newly discovered SKILL.md file to internal storage.
  // Performs deduplication by skill name (skips if name already exists).
  // Returns true if the metadata was successfully added, false if skipped
  // (duplicate name or parse error).
  bool AddOneMetaData(const std::string& skill_file);

  // Remove metadata from internal storage by skill name.
  // Returns true if the metadata was successfully removed, false if not found.
  bool RemoveOneMetaData(const std::string& skill_name);

  // Rewrite (update) metadata in internal storage from a SKILL.md file.
  // Finds the skill by name and replaces it with freshly loaded metadata.
  // Returns true if successfully updated, false if the skill was not found
  // or parse error occurred.
  bool RewriteOneMetaData(const std::string& skill_file);

  // Merge metadata from multiple skills into a single context string.
  // Formats each skill with emoji, name, description, resource dirs, and
  // commands. Suitable for displaying skill summaries or building prompts.
  std::string MergeSkillContext(const std::vector<SkillMetadata>& metas) const;

  // Merge all internally cached skill metadata into a single context string.
  // Convenience wrapper that uses skills_meta_ as the source.
  std::string MergeSkillContext() const;

  // Get all loaded metadata from internal storage.
  // Returns a copy of the internally cached skill metadata.
  std::vector<SkillMetadata> GetAllMetaData() const;

  // Check if all metadata has been loaded (scan complete).
  // Returns true if LoaderAllMetaData has been called, false otherwise.
  bool IsLoadAllMetaData() const;

  // Find metadata by skill name.
  // Returns a copy of the metadata if found, std::nullopt otherwise.
  // Value-return semantics eliminate dangling-pointer risk when the
  // internal vector is concurrently modified by another thread.
  std::optional<SkillMetadata> FindMetaDataByName(const std::string& skill_name) const;

  // Find metadata by skill key (alternative identifier in extra metadata).
  // Returns a copy of the metadata if found, std::nullopt otherwise.
  std::optional<SkillMetadata> FindMetaDataByKey(const std::string& skill_key) const;

  // Find metadata by skill file path.
  // Extracts name (directory name) and root_dir (parent path) from the file
  // path and matches against cached metadata. Returns a copy of the metadata
  // if found, std::nullopt otherwise.
  std::optional<SkillMetadata> FindMetaDataByFile(const std::string& skill_file) const;

 private:
  // Scan all SKILL.md files under a directory and extract metadata.
  std::vector<SkillMetadata> LoadMetaDataFromDirectory(const std::string& skills_dir) const;
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex skills_meta_mutex_;  // protects skills_meta_ & skills_scan_complete_
  std::vector<SkillMetadata> skills_meta_;  // GUARD: skills_meta_mutex_
  bool skills_scan_complete_ = false;      // GUARD: skills_meta_mutex_
};

}  // namespace quantclaw

  