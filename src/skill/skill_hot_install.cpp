// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/skill/skill_hot_install.hpp"

#include <algorithm>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "quantclaw/platform/process.hpp"

namespace quantclaw {

SkillHotInstall::SkillHotInstall(SkillLoaderMeta& meta_loader,
                                 std::shared_ptr<spdlog::logger> logger)
    : meta_loader_(meta_loader),
      logger_(std::move(logger)) {
  logger_->info("SkillHotInstall initialized");
}

SkillHotInstall::~SkillHotInstall() {
  StopWatching();
}

// ---------------------------------------------------------------------------
// Hot loading — SkillMeta list management
// ---------------------------------------------------------------------------

void SkillHotInstall::SetKnownMetadatas(
    const std::vector<SkillMetadata>& metas) {
  std::lock_guard<std::mutex> lock(metas_mutex_);
  known_metas_ = metas;
  known_names_.clear();
  for (const auto& meta : known_metas_) {
    known_names_.insert(meta.mini.name);
  }

  // Also record the file paths for change detection
  {
    std::lock_guard<std::mutex> paths_lock(paths_mutex_);
    known_paths_.clear();
    for (const auto& meta : known_metas_) {
      if (!meta.mini.root_dir.empty()) {
        auto skill_file = std::filesystem::path(meta.mini.root_dir) / "SKILL.md";
        known_paths_.insert(skill_file.string());
      }
    }
  }

  logger_->info("SetKnownMetadatas: {} skills registered", metas.size());
}

std::vector<SkillMetadata> SkillHotInstall::GetKnownMetadatas() const {
  std::lock_guard<std::mutex> lock(metas_mutex_);
  return known_metas_;
}

int SkillHotInstall::ScanForNewSkills() {
  auto dirs = BuildDirectoryList(skills_config_, workspace_path_);

  // Record current mtimes for all directories
  for (const auto& dir : dirs) {
    RecordFileMtimes(dir);
  }

  std::vector<SkillMetadata> new_metas;
  for (const auto& dir : dirs) {
    ScanDirectory(dir, new_metas);
  }

  if (new_metas.empty()) {
    return 0;
  }

  // Append new metas to the known list and collect those that should fire
  // the callback. Callbacks are invoked AFTER releasing metas_mutex_ to
  // prevent A→B→A deadlock (callback may call GetKnownMetadatas which
  // reacquires metas_mutex_).
  SkillMetaDiscoveredCallback cb_copy;
  std::vector<SkillMetadata> added_metas;
  {
    std::lock_guard<std::mutex> lock(metas_mutex_);
    for (auto& meta : new_metas) {
      // Dedup check (may have been added by a concurrent scan)
      if (known_names_.count(meta.mini.name)) {
        logger_->debug("ScanForNewSkills: '{}' already known, skipping",
                       meta.mini.name);
        continue;
      }

      // Check per-skill disable
      auto it = skills_config_.entries.find(meta.mini.name);
      if (it != skills_config_.entries.end() && !it->second.enabled) {
        logger_->debug("ScanForNewSkills: '{}' disabled via config",
                       meta.mini.name);
        continue;
      }

      known_names_.insert(meta.mini.name);
      known_metas_.push_back(meta);
      added_metas.push_back(meta);  // snapshot for post-lock callback
    }

    // Grab callback copy while still under metas_mutex_ for consistency,
    // but do NOT invoke it here (deadlock risk).
    {
      std::lock_guard<std::mutex> cb_lock(callback_mutex_);
      cb_copy = meta_discovered_cb_;
    }
  }
  // Fire callbacks outside the lock — safe to call GetKnownMetadatas now
  if (cb_copy) {
    for (const auto& meta : added_metas) {
      cb_copy(meta);
    }
  }

  int count = 0;
  {
    std::lock_guard<std::mutex> lock(metas_mutex_);
    count = static_cast<int>(known_metas_.size());
  }
  logger_->info("ScanForNewSkills: discovered {} new skill(s), total now {}",
                new_metas.size(), count);
  return static_cast<int>(new_metas.size());
}

void SkillHotInstall::SetMetaDiscoveredCallback(
    SkillMetaDiscoveredCallback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  meta_discovered_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// File change detection — background watcher
// ---------------------------------------------------------------------------

void SkillHotInstall::StartWatching(
    const SkillsConfig& skills_config,
    const std::string& workspace_path,
    std::chrono::seconds poll_interval) {
  if (watching_) {
    logger_->warn("SkillHotInstall: watcher already running");
    return;
  }

  skills_config_ = skills_config;
  workspace_path_ = workspace_path;
  poll_interval_ = poll_interval;

  // Record initial file mtimes
  auto dirs = BuildDirectoryList(skills_config_, workspace_path_);
  for (const auto& dir : dirs) {
    RecordFileMtimes(dir);
  }

  watching_ = true;
  watcher_thread_ = std::make_unique<std::thread>([this]() {
    logger_->info("SkillHotInstall: watcher thread started (interval={}s)",
                  poll_interval_.count());

    while (watching_) {
      std::this_thread::sleep_for(poll_interval_);
      if (!watching_) {
        break;
      }

      // Scan for new and modified skill files
      auto dirs = BuildDirectoryList(skills_config_, workspace_path_);

      for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) {
          continue;
        }

        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(dir)) {
          if (!entry.is_regular_file() ||
              entry.path().filename() != "SKILL.md") {
            continue;
          }

          std::string path_str = entry.path().string();
          auto mtime = std::filesystem::last_write_time(entry.path());

          bool is_new = false;
          bool is_modified = false;
          {
            std::lock_guard<std::mutex> lock(paths_mutex_);
            auto path_it = known_paths_.find(path_str);
            if (path_it == known_paths_.end()) {
              is_new = true;
            } else {
              auto mtime_it = file_mtimes_.find(path_str);
              if (mtime_it != file_mtimes_.end() &&
                  mtime_it->second != mtime) {
                is_modified = true;
              }
            }
          }

          if (is_new) {
            logger_->info("SkillHotInstall: new SKILL.md detected: {}",
                          path_str);

            // Load metadata for the new skill
            try {
              if (!meta_loader_.AddOneMetaData(path_str)) {
                continue;
              }
              auto meta_opt = meta_loader_.FindMetaDataByFile(path_str);
              if (!meta_opt) {
                logger_->error("SkillHotInstall: failed to find metadata for new skill at {}", path_str);
                continue;
              }
              SkillMetadata meta_copy = std::move(*meta_opt);
              // Check dedup and config filter
              bool should_add = false;
              {
                std::lock_guard<std::mutex> meta_lock(metas_mutex_);
                if (!known_names_.count(meta_copy.mini.name)) {
                  auto cfg_it = skills_config_.entries.find(meta_copy.mini.name);
                  if (cfg_it == skills_config_.entries.end() ||
                      cfg_it->second.enabled) {
                    known_names_.insert(meta_copy.mini.name);

                    known_metas_.push_back(meta_copy);
                    
                    should_add = true;
                  }
                }
              }

              if (should_add) {
                // Update known paths and mtimes
                {
                  std::lock_guard<std::mutex> paths_lock(paths_mutex_);
                  known_paths_.insert(path_str);
                  file_mtimes_[path_str] = mtime;
                }

                // Fire callbacks
                SkillFileChangeCallback file_cb;
                SkillMetaDiscoveredCallback meta_cb;
                {
                  std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                  file_cb = file_change_cb_;
                  meta_cb = meta_discovered_cb_;
                }
                if (file_cb) {
                  file_cb(path_str);
                }
                if (meta_cb) {
                  meta_cb(meta_copy);
                }
              }
            } catch (const std::exception& e) {
              logger_->error(
                  "SkillHotInstall: failed to load new skill from {}: {}",
                  path_str, e.what());
            }
          } else if (is_modified) {
            logger_->info("SkillHotInstall: modified SKILL.md detected: {}",
                          path_str);

            // Update mtime
            {
              std::lock_guard<std::mutex> paths_lock(paths_mutex_);
              file_mtimes_[path_str] = mtime;
            }

            // Fire file change callback
            SkillFileChangeCallback file_cb;
            {
              std::lock_guard<std::mutex> cb_lock(callback_mutex_);
              file_cb = file_change_cb_;
            }
            if (file_cb) {
              file_cb(path_str);
            }
          }
        }
      }
    }

    logger_->info("SkillHotInstall: watcher thread stopped");
  });

  logger_->info("SkillHotInstall: watching started for {} directories",
                dirs.size());
}

void SkillHotInstall::StopWatching() {
  if (!watching_) {
    return;
  }
  watching_ = false;
  if (watcher_thread_ && watcher_thread_->joinable()) {
    watcher_thread_->join();
  }
  watcher_thread_.reset();
  logger_->info("SkillHotInstall: watching stopped");
}

bool SkillHotInstall::IsWatching() const {
  return watching_.load();
}

void SkillHotInstall::SetFileChangeCallback(SkillFileChangeCallback cb) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  file_change_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<std::string> SkillHotInstall::BuildDirectoryList(
    const SkillsConfig& skills_config,
    const std::string& workspace_path) const {
  std::vector<std::string> dirs;
  dirs.push_back(
      (std::filesystem::path(workspace_path) / "skills").string());
  dirs.push_back(
      (std::filesystem::path(platform::home_directory()) / ".quantclaw" /
       "skills")
          .string());
  for (const auto& extra : skills_config.load.extra_dirs) {
    dirs.push_back(extra);
  }
  return dirs;
}

void SkillHotInstall::ScanDirectory(
    const std::string& dir, std::vector<SkillMetadata>& new_metas) {
  if (!std::filesystem::exists(dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() ||
        entry.path().filename() != "SKILL.md") {
      continue;
    }

    std::string path_str = entry.path().string();

    // Skip if already known
    {
      std::lock_guard<std::mutex> lock(paths_mutex_);
      if (known_paths_.count(path_str)) {
        continue;
      }
    }

    try {
      auto meta = meta_loader_.LoaderOneMetaData(path_str);
      new_metas.push_back(std::move(meta));

      // Add to known paths
      {
        std::lock_guard<std::mutex> lock(paths_mutex_);
        known_paths_.insert(path_str);
        if (std::filesystem::exists(entry.path())) {
          file_mtimes_[path_str] =
              std::filesystem::last_write_time(entry.path());
        }
      }
    } catch (const std::exception& e) {
      logger_->error("ScanDirectory: failed to load skill from {}: {}",
                     path_str, e.what());
    }
  }
}

void SkillHotInstall::RecordFileMtimes(const std::string& dir) {
  if (!std::filesystem::exists(dir)) {
    return;
  }

  std::lock_guard<std::mutex> lock(paths_mutex_);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() ||
        entry.path().filename() != "SKILL.md") {
      continue;
    }

    std::string path_str = entry.path().string();
    known_paths_.insert(path_str);
    file_mtimes_[path_str] = std::filesystem::last_write_time(entry.path());
  }
}

}  // namespace quantclaw
