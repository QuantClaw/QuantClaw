// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"

namespace quantclaw {

class MemoryManager {
 public:
  explicit MemoryManager(const std::filesystem::path& workspace_path,
                         std::shared_ptr<spdlog::logger> logger);
  ~MemoryManager();

  // Load all workspace files into memory
  void LoadWorkspaceFiles();

  // Read identity files (SOUL.md, USER.md)
  std::string ReadIdentityFile(const std::string& filename) const;

  // Read AGENTS.md (OpenClaw behavior instructions)
  std::string ReadAgentsFile() const;

  // Read TOOLS.md (OpenClaw tool usage guide)
  std::string ReadToolsFile() const;

  // Search memory files for content
  std::vector<std::string> SearchMemory(const std::string& query) const;

  // Save daily memory entry
  void SaveDailyMemory(const std::string& content);

  // File change callback type
  using FileChangeCallback = std::function<void(const std::string& filename)>;

  // Start file system watcher (polling)
  void StartFileWatcher();

  // Stop file system watcher
  void StopFileWatcher();

  // Set callback for file changes
  void SetFileChangeCallback(FileChangeCallback cb);

  // Get workspace path
  const std::filesystem::path& GetWorkspacePath() const;

  // Set workspace for a specific agent ID
  void SetAgentWorkspace(const std::string& agent_id);

  // Get base QuantClaw directory (~/.quantclaw)
  std::filesystem::path GetBaseDir() const;

  // Get sessions directory for an agent
  std::filesystem::path
  GetSessionsDir(const std::string& agent_id = "main") const;

 private:
  bool is_memory_file(const std::filesystem::path& filepath) const;
  std::string read_file_content(const std::filesystem::path& filepath) const;
  void write_file_content(const std::filesystem::path& filepath,
                          const std::string& content) const;

  std::filesystem::path workspace_path_;
  std::filesystem::path base_dir_;  // ~/.quantclaw
  std::string agent_id_ = "default";
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::shared_mutex cache_mutex_;

  // File watcher
  std::unique_ptr<std::thread> watcher_thread_;
  std::atomic<bool> watching_{false};
  mutable std::mutex
      watcher_mutex_;  // Protects change_callback_ and file_mtimes_
  FileChangeCallback change_callback_;
  std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;
};

}  // namespace quantclaw
