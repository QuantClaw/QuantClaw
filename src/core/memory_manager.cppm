// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.memory_manager;

import std;

export namespace quantclaw {

class MemoryManager {
 public:
  explicit MemoryManager(const std::filesystem::path& workspace_path,
                         std::shared_ptr<spdlog::logger> logger);
  ~MemoryManager();

  void LoadWorkspaceFiles();
  std::string ReadIdentityFile(const std::string& filename) const;
  std::string ReadAgentsFile() const;
  std::string ReadToolsFile() const;
  std::vector<std::string> SearchMemory(const std::string& query) const;
  void SaveDailyMemory(const std::string& content);

  using FileChangeCallback = std::function<void(const std::string& filename)>;

  void StartFileWatcher();
  void StopFileWatcher();
  void SetFileChangeCallback(FileChangeCallback cb);

  const std::filesystem::path& GetWorkspacePath() const;
  void SetAgentWorkspace(const std::string& agent_id);
  std::filesystem::path GetBaseDir() const;
  std::filesystem::path GetSessionsDir(const std::string& agent_id = "main") const;

 private:
  bool is_memory_file(const std::filesystem::path& filepath) const;
  std::string read_file_content(const std::filesystem::path& filepath) const;
  void write_file_content(const std::filesystem::path& filepath,
                          const std::string& content) const;

  std::filesystem::path workspace_path_;
  std::filesystem::path base_dir_;
  std::string agent_id_ = "default";
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::shared_mutex cache_mutex_;

  std::unique_ptr<std::thread> watcher_thread_;
  std::atomic<bool> watching_{false};
  mutable std::mutex watcher_mutex_;
  FileChangeCallback change_callback_;
  // Self-pipe used to unblock the inotify poll loop on shutdown.
  // [0] = read end (owned by watcher thread), [1] = write end (written by StopFileWatcher).
  // Both are initialised to -1 and opened only while the watcher is active.
  int wakeup_pipe_[2]{-1, -1};
};

}  // namespace quantclaw