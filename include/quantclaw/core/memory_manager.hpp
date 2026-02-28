#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include "quantclaw/config.hpp"
#include <spdlog/spdlog.h>

namespace quantclaw {

class MemoryManager {
public:
    explicit MemoryManager(const std::filesystem::path& workspace_path,
                           std::shared_ptr<spdlog::logger> logger);
    ~MemoryManager();

    // Load all workspace files into memory
    void load_workspace_files();

    // Read identity files (SOUL.md, USER.md)
    std::string read_identity_file(const std::string& filename);

    // Read AGENTS.md (OpenClaw behavior instructions)
    std::string read_agents_file();

    // Read TOOLS.md (OpenClaw tool usage guide)
    std::string read_tools_file();

    // Search memory files for content
    std::vector<std::string> search_memory(const std::string& query);

    // Save daily memory entry
    void save_daily_memory(const std::string& content);

    // File change callback type
    using FileChangeCallback = std::function<void(const std::string& filename)>;

    // Start file system watcher (polling)
    void start_file_watcher();

    // Stop file system watcher
    void stop_file_watcher();

    // Set callback for file changes
    void set_file_change_callback(FileChangeCallback cb);

    // Get workspace path
    const std::filesystem::path& get_workspace_path() const;

    // Set workspace for a specific agent ID
    void set_agent_workspace(const std::string& agent_id);

    // Get base QuantClaw directory (~/.quantclaw)
    std::filesystem::path get_base_dir() const;

    // Get sessions directory for an agent
    std::filesystem::path get_sessions_dir(const std::string& agent_id = "main") const;

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
    FileChangeCallback change_callback_;
    std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;
};

} // namespace quantclaw
