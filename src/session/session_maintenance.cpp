// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/session/session_maintenance.hpp"

#include <algorithm>
#include <fstream>
#include <regex>

namespace quantclaw {

// --- MaintenanceMode ---

MaintenanceMode MaintenanceModeFromString(const std::string& s) {
  if (s == "warn")
    return MaintenanceMode::kWarn;
  return MaintenanceMode::kEnforce;
}

// --- SessionMaintenanceConfig ---

SessionMaintenanceConfig
SessionMaintenanceConfig::FromJson(const nlohmann::json& j) {
  SessionMaintenanceConfig c;
  if (j.contains("mode") && j["mode"].is_string()) {
    c.mode = MaintenanceModeFromString(j["mode"].get<std::string>());
  }
  if (j.contains("pruneAfter")) {
    if (j["pruneAfter"].is_string()) {
      c.prune_after_seconds = SessionMaintenance::ParseDurationSeconds(
          j["pruneAfter"].get<std::string>());
    } else if (j["pruneAfter"].is_number()) {
      c.prune_after_seconds = j["pruneAfter"].get<int>();
    }
  }
  c.max_entries = j.value("maxEntries", 0);
  if (j.contains("rotateBytes")) {
    if (j["rotateBytes"].is_string()) {
      c.rotate_bytes = SessionMaintenance::ParseSizeBytes(
          j["rotateBytes"].get<std::string>());
    } else if (j["rotateBytes"].is_number()) {
      c.rotate_bytes = j["rotateBytes"].get<int64_t>();
    }
  }
  if (j.contains("maxDiskBytes")) {
    if (j["maxDiskBytes"].is_string()) {
      c.max_disk_bytes = SessionMaintenance::ParseSizeBytes(
          j["maxDiskBytes"].get<std::string>());
    } else if (j["maxDiskBytes"].is_number()) {
      c.max_disk_bytes = j["maxDiskBytes"].get<int64_t>();
    }
  }
  c.sweep_interval_seconds = j.value("sweepInterval", 300);
  return c;
}

// --- SessionMaintenance ---

SessionMaintenance::SessionMaintenance(
    const std::filesystem::path& sessions_dir,
    std::shared_ptr<spdlog::logger> logger)
    : sessions_dir_(sessions_dir),
      logger_(std::move(logger)),
      last_sweep_(std::chrono::steady_clock::time_point{}) {}

void SessionMaintenance::Configure(const SessionMaintenanceConfig& config) {
  config_ = config;
}

MaintenanceResult SessionMaintenance::Sweep(bool force) {
  MaintenanceResult result;

  // Check sweep interval
  auto now = std::chrono::steady_clock::now();
  if (!force) {
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_sweep_);
    if (elapsed.count() < config_.sweep_interval_seconds) {
      return result;  // Too soon
    }
  }
  last_sweep_ = now;

  if (!std::filesystem::exists(sessions_dir_)) {
    return result;
  }

  result.swept = true;

  // Run maintenance steps
  if (config_.prune_after_seconds > 0) {
    prune_old_sessions(result);
  }
  if (config_.max_entries > 0) {
    enforce_max_entries(result);
  }
  if (config_.rotate_bytes > 0) {
    rotate_large_files(result);
  }
  if (config_.max_disk_bytes > 0) {
    enforce_disk_limit(result);
  }

  if (result.pruned_count > 0 || result.rotated_count > 0) {
    logger_->info("Maintenance: pruned={}, rotated={}, freed={}B",
                  result.pruned_count, result.rotated_count,
                  result.bytes_freed);
  }

  return result;
}

int SessionMaintenance::ParseDurationSeconds(const std::string& s) {
  if (s.empty())
    return 0;

  std::regex re(R"((\d+)\s*(s|m|h|d|w))");
  std::smatch match;
  if (!std::regex_match(s, match, re)) {
    // Try plain number (seconds)
    try {
      return std::stoi(s);
    } catch (const std::exception&) {
      return 0;
    }
  }

  int value = std::stoi(match[1].str());
  std::string unit = match[2].str();
  if (unit == "s")
    return value;
  if (unit == "m")
    return value * 60;
  if (unit == "h")
    return value * 3600;
  if (unit == "d")
    return value * 86400;
  if (unit == "w")
    return value * 604800;
  return value;
}

int64_t SessionMaintenance::ParseSizeBytes(const std::string& s) {
  if (s.empty())
    return 0;

  std::regex re(R"((\d+)\s*(B|KB|MB|GB|TB)?)", std::regex::icase);
  std::smatch match;
  if (!std::regex_match(s, match, re)) {
    try {
      return std::stoll(s);
    } catch (const std::exception&) {
      return 0;
    }
  }

  int64_t value = std::stoll(match[1].str());
  std::string unit = match[2].str();
  // Normalize to uppercase
  std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);

  if (unit.empty() || unit == "B")
    return value;
  if (unit == "KB")
    return value * 1024;
  if (unit == "MB")
    return value * 1024 * 1024;
  if (unit == "GB")
    return value * 1024LL * 1024 * 1024;
  if (unit == "TB")
    return value * 1024LL * 1024 * 1024 * 1024;
  return value;
}

int SessionMaintenance::prune_old_sessions(MaintenanceResult& result) {
  auto now = std::chrono::system_clock::now();
  auto cutoff = now - std::chrono::seconds(config_.prune_after_seconds);
  auto files = get_session_files();
  int count = 0;

  for (const auto& info : files) {
    if (info.mtime < cutoff) {
      if (config_.mode == MaintenanceMode::kWarn) {
        result.warnings.push_back("Would prune: " +
                                  info.path.filename().string());
        continue;
      }
      try {
        result.bytes_freed += info.size;
        std::filesystem::remove(info.path);
        ++count;
      } catch (const std::exception& e) {
        logger_->warn("Failed to prune {}: {}", info.path.string(), e.what());
      }
    }
  }

  result.pruned_count += count;
  return count;
}

int SessionMaintenance::enforce_max_entries(MaintenanceResult& result) {
  auto files = get_session_files();
  int total = static_cast<int>(files.size());
  if (total <= config_.max_entries)
    return 0;

  int to_remove = total - config_.max_entries;
  int count = 0;

  // Remove oldest first (files are sorted oldest-first)
  for (int i = 0; i < to_remove && i < static_cast<int>(files.size()); ++i) {
    if (config_.mode == MaintenanceMode::kWarn) {
      result.warnings.push_back("Would prune (max entries): " +
                                files[i].path.filename().string());
      continue;
    }
    try {
      result.bytes_freed += files[i].size;
      std::filesystem::remove(files[i].path);
      ++count;
    } catch (const std::exception& e) {
      logger_->warn("Failed to remove {}: {}", files[i].path.string(),
                    e.what());
    }
  }

  result.pruned_count += count;
  return count;
}

int SessionMaintenance::rotate_large_files(MaintenanceResult& result) {
  int count = 0;

  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() != ".jsonl")
      continue;

    auto size = static_cast<int64_t>(entry.file_size());
    if (size <= config_.rotate_bytes)
      continue;

    if (config_.mode == MaintenanceMode::kWarn) {
      result.warnings.push_back(
          "Would rotate: " + entry.path().filename().string() + " (" +
          std::to_string(size) + " bytes)");
      continue;
    }

    archive_file(entry.path());
    // Truncate the original file
    std::ofstream ofs(entry.path(), std::ios::trunc);
    ofs.close();

    result.bytes_freed += size;
    ++count;
  }

  result.rotated_count += count;
  return count;
}

void SessionMaintenance::enforce_disk_limit(MaintenanceResult& result) {
  int64_t total = total_session_size();
  if (total <= config_.max_disk_bytes)
    return;

  auto files = get_session_files();
  // Remove oldest until under limit
  for (const auto& info : files) {
    if (total <= config_.max_disk_bytes)
      break;

    if (config_.mode == MaintenanceMode::kWarn) {
      result.warnings.push_back("Would prune (disk limit): " +
                                info.path.filename().string());
      total -= info.size;
      continue;
    }

    try {
      total -= info.size;
      result.bytes_freed += info.size;
      std::filesystem::remove(info.path);
      ++result.pruned_count;
    } catch (const std::exception& e) {
      logger_->warn("Failed to remove {}: {}", info.path.string(), e.what());
    }
  }
}

void SessionMaintenance::archive_file(const std::filesystem::path& path) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  struct tm tm {};
#ifdef _WIN32
  localtime_s(&tm, &time_t);
#else
  localtime_r(&time_t, &tm);
#endif

  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);

  auto archive_name = path.stem().string() + "." + buf + ".archived.jsonl";
  auto archive_path = path.parent_path() / archive_name;

  try {
    std::filesystem::rename(path, archive_path);
    logger_->info("Archived {} -> {}", path.filename().string(), archive_name);
  } catch (const std::exception& e) {
    logger_->warn("Failed to archive {}: {}", path.string(), e.what());
  }
}

int64_t SessionMaintenance::total_session_size() const {
  int64_t total = 0;
  if (!std::filesystem::exists(sessions_dir_))
    return 0;
  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
    if (entry.is_regular_file()) {
      total += static_cast<int64_t>(entry.file_size());
    }
  }
  return total;
}

std::vector<SessionMaintenance::SessionFileInfo>
SessionMaintenance::get_session_files() const {
  std::vector<SessionFileInfo> files;
  if (!std::filesystem::exists(sessions_dir_))
    return files;

  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() != ".jsonl" &&
        entry.path().extension() != ".json") {
      continue;
    }

    SessionFileInfo info;
    info.path = entry.path();
    info.session_key = entry.path().stem().string();
    auto ftime = entry.last_write_time();
    // Convert file_time to system_clock
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    info.mtime = sctp;
    info.size = static_cast<int64_t>(entry.file_size());
    files.push_back(info);
  }

  // Sort by mtime (oldest first)
  std::sort(files.begin(), files.end(),
            [](const SessionFileInfo& a, const SessionFileInfo& b) {
              return a.mtime < b.mtime;
            });

  return files;
}

}  // namespace quantclaw
