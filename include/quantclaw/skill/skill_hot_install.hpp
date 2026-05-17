// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/skill/skill_loader_meta.hpp"
#include "quantclaw/skill/skill_parse_file.hpp"

namespace quantclaw {

// ---------------------------------------------------------------------------
// SkillHotInstall: skill hot loading and file change detection.
//
// Responsibilities:
//   1. Hot loading — monitors watched skill directories for new/changed
//      SKILL.md files; when detected, loads their SkillMetadata and appends
//      to the live metadata list.
//   2. File change detection — tracks known SKILL.md paths and their
//      modification times; detects additions and modifications.
//
// Usage:
//   SkillLoaderMeta meta_loader(logger);
//   SkillHotInstall hot_install(meta_loader, logger);
//
//   // Initial load
//   auto metas = meta_loader.LoaderAllMetaData(config, workspace);
//   hot_install.SetKnownMetadatas(metas);
//
//   // Start watching (non-blocking)
//   hot_install.StartWatching(config, workspace);
//
//   // Stop watching (blocking until watcher thread exits)
//   hot_install.StopWatching();
// ---------------------------------------------------------------------------

// Callback types for skill hot-load events.
// |skill_path| is the absolute path to the new/changed SKILL.md file.
using SkillFileChangeCallback =
    std::function<void(const std::string& skill_path)>;

// Callback invoked when a new SkillMetadata is discovered.
// |metadata| is the newly loaded metadata; the receiver may append it to
// the live SkillMeta list.
using SkillMetaDiscoveredCallback =
    std::function<void(const SkillMetadata& metadata)>;

class SkillHotInstall {
 public:
  // Construct with reference to the meta loader.
  explicit SkillHotInstall(SkillLoaderMeta& meta_loader,
                           std::shared_ptr<spdlog::logger> logger);

  ~SkillHotInstall();

  // -----------------------------------------------------------------------
  // Hot loading — SkillMeta list management
  // -----------------------------------------------------------------------

  // Set the initial known metadata list (typically from LoaderAllMetaData).
  // This establishes the baseline; subsequent calls to ScanForNewSkills will
  // detect additions relative to this set.
  void SetKnownMetadatas(const std::vector<SkillMetadata>& metas);

  // Get the current live metadata list (thread-safe copy).
  std::vector<SkillMetadata> GetKnownMetadatas() const;

  // Perform a one-shot scan of all watched directories for new SKILL.md files.
  // New skills' metadata is loaded and appended to the live list.
  // Returns the number of newly discovered skills.
  int ScanForNewSkills();

  // Set callback invoked when a new SkillMetadata is discovered during scan.
  void SetMetaDiscoveredCallback(SkillMetaDiscoveredCallback cb);

  // -----------------------------------------------------------------------
  // File change detection — background watcher
  // -----------------------------------------------------------------------

  // Start the background watcher thread. Periodically scans watched
  // directories for new/modified SKILL.md files and hot-loads them.
  // |poll_interval| controls scan frequency (default 5 seconds).
  void StartWatching(
      const SkillsConfig& skills_config,
      const std::string& workspace_path,
      std::chrono::seconds poll_interval = std::chrono::seconds(5));

  // Stop the background watcher thread (blocks until thread exits).
  void StopWatching();

  // Check if the watcher is currently running.
  bool IsWatching() const;

  // Set callback invoked when a SKILL.md file is created or modified.
  void SetFileChangeCallback(SkillFileChangeCallback cb);

 private:
  // Build the ordered list of skill directories to scan.
  std::vector<std::string> BuildDirectoryList(
      const SkillsConfig& skills_config,
      const std::string& workspace_path) const;

  // Scan a single directory for SKILL.md files not yet in |known_paths_|
  // or with updated modification times.
  void ScanDirectory(const std::string& dir,
                     std::vector<SkillMetadata>& new_metas);

  // Record file mtimes for all known SKILL.md files under a directory.
  void RecordFileMtimes(const std::string& dir);

  // Reference to the meta loader
  SkillLoaderMeta& meta_loader_;
  std::shared_ptr<spdlog::logger> logger_;

  // Configuration for the watcher (captured at StartWatching time)
  SkillsConfig skills_config_;
  std::string workspace_path_;
  std::chrono::seconds poll_interval_{5};

  // Known skill metadata (thread-safe)
  mutable std::mutex metas_mutex_;
  std::vector<SkillMetadata> known_metas_;
  std::unordered_set<std::string> known_names_;  // dedup by skill name

  // Known SKILL.md file paths and their modification times
  mutable std::mutex paths_mutex_;
  std::unordered_set<std::string> known_paths_;
  std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;

  // Background watcher
  std::unique_ptr<std::thread> watcher_thread_;
  std::atomic<bool> watching_{false};

  // Callbacks
  mutable std::mutex callback_mutex_;
  SkillMetaDiscoveredCallback meta_discovered_cb_;
  SkillFileChangeCallback file_change_cb_;
};

}  // namespace quantclaw
