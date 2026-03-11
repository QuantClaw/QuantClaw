// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// Maintenance mode (compatible with OpenClaw session.maintenance)
enum class MaintenanceMode {
  kEnforce,  // Actively prune and rotate
  kWarn,     // Log warnings only
};

MaintenanceMode MaintenanceModeFromString(const std::string& s);

// Session maintenance configuration
struct SessionMaintenanceConfig {
  MaintenanceMode mode = MaintenanceMode::kEnforce;

  // Prune sessions older than this duration (0 = disabled)
  // Parsed from strings like "7d", "168h", "2w"
  int prune_after_seconds = 0;

  // Maximum number of session entries (0 = unlimited)
  int max_entries = 0;

  // Rotate transcript files exceeding this size in bytes (0 = disabled)
  // Parsed from strings like "10MB", "1GB"
  int64_t rotate_bytes = 0;

  // Max total disk usage for all session files (0 = unlimited)
  int64_t max_disk_bytes = 0;

  // Minimum interval between maintenance sweeps (seconds)
  int sweep_interval_seconds = 300;  // 5 minutes

  static SessionMaintenanceConfig FromJson(const nlohmann::json& j);
};

// Result of a maintenance sweep
struct MaintenanceResult {
  bool swept = false;
  int pruned_count = 0;
  int rotated_count = 0;
  int64_t bytes_freed = 0;
  std::vector<std::string> warnings;
};

// Performs session file maintenance: pruning, rotation, and disk management
class SessionMaintenance {
 public:
  SessionMaintenance(const std::filesystem::path& sessions_dir,
                     std::shared_ptr<spdlog::logger> logger);

  // Configure maintenance settings
  void Configure(const SessionMaintenanceConfig& config);

  // Run a maintenance sweep. Returns result of actions taken.
  // Will skip if called too soon after last sweep (respects sweep_interval).
  MaintenanceResult Sweep(bool force = false);

  // Parse a duration string ("7d", "168h", "2w") to seconds
  static int ParseDurationSeconds(const std::string& s);

  // Parse a size string ("10MB", "1GB") to bytes
  static int64_t ParseSizeBytes(const std::string& s);

  // Get current config
  const SessionMaintenanceConfig& GetConfig() const {
    return config_;
  }

 private:
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  SessionMaintenanceConfig config_;
  std::chrono::steady_clock::time_point last_sweep_;

  // Prune sessions older than prune_after
  int prune_old_sessions(MaintenanceResult& result);

  // Enforce max_entries limit
  int enforce_max_entries(MaintenanceResult& result);

  // Rotate oversized transcript files
  int rotate_large_files(MaintenanceResult& result);

  // Enforce max total disk usage
  void enforce_disk_limit(MaintenanceResult& result);

  // Archive a transcript file (rename with timestamp suffix)
  void archive_file(const std::filesystem::path& path);

  // Get total size of session directory
  int64_t total_session_size() const;

  // Get session file info sorted by modification time (oldest first)
  struct SessionFileInfo {
    std::filesystem::path path;
    std::string session_key;
    std::chrono::system_clock::time_point mtime;
    int64_t size;
  };
  std::vector<SessionFileInfo> get_session_files() const;
};

}  // namespace quantclaw
