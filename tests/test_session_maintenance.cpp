// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/session/session_maintenance.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

class SessionMaintenanceTest : public ::testing::Test {
 protected:
  std::filesystem::path test_dir_;

  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("qc_maint_test");
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  void create_session_file(const std::string& name,
                           const std::string& content) {
    std::ofstream ofs(test_dir_ / name);
    ofs << content;
  }

  void create_session_file_with_size(const std::string& name, size_t bytes) {
    std::ofstream ofs(test_dir_ / name);
    std::string data(bytes, 'x');
    ofs << data;
  }
};

// --- Duration parsing ---

TEST(DurationParseTest, Seconds) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("30s"), 30);
}

TEST(DurationParseTest, Minutes) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("5m"), 300);
}

TEST(DurationParseTest, Hours) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("24h"), 86400);
}

TEST(DurationParseTest, Days) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("7d"), 604800);
}

TEST(DurationParseTest, Weeks) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("2w"), 1209600);
}

TEST(DurationParseTest, PlainNumber) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds("3600"), 3600);
}

TEST(DurationParseTest, Empty) {
  EXPECT_EQ(SessionMaintenance::ParseDurationSeconds(""), 0);
}

// --- Size parsing ---

TEST(SizeParseTest, Bytes) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("1024B"), 1024);
}

TEST(SizeParseTest, Kilobytes) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("1KB"), 1024);
}

TEST(SizeParseTest, Megabytes) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("10MB"), 10 * 1024 * 1024);
}

TEST(SizeParseTest, Gigabytes) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("1GB"), 1024LL * 1024 * 1024);
}

TEST(SizeParseTest, PlainNumber) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("1048576"), 1048576);
}

TEST(SizeParseTest, CaseInsensitive) {
  EXPECT_EQ(SessionMaintenance::ParseSizeBytes("10mb"), 10 * 1024 * 1024);
}

// --- Config ---

TEST(SessionMaintenanceConfigTest, FromJson) {
  nlohmann::json j = {
      {"mode", "enforce"},     {"pruneAfter", "7d"},    {"maxEntries", 100},
      {"rotateBytes", "10MB"}, {"maxDiskBytes", "1GB"}, {"sweepInterval", 600},
  };
  auto c = SessionMaintenanceConfig::FromJson(j);
  EXPECT_EQ(c.mode, MaintenanceMode::kEnforce);
  EXPECT_EQ(c.prune_after_seconds, 604800);
  EXPECT_EQ(c.max_entries, 100);
  EXPECT_EQ(c.rotate_bytes, 10 * 1024 * 1024);
  EXPECT_EQ(c.max_disk_bytes, 1024LL * 1024 * 1024);
  EXPECT_EQ(c.sweep_interval_seconds, 600);
}

TEST(SessionMaintenanceConfigTest, WarnMode) {
  nlohmann::json j = {{"mode", "warn"}};
  auto c = SessionMaintenanceConfig::FromJson(j);
  EXPECT_EQ(c.mode, MaintenanceMode::kWarn);
}

TEST(SessionMaintenanceConfigTest, NumericValues) {
  nlohmann::json j = {
      {"pruneAfter", 86400},
      {"rotateBytes", 1048576},
  };
  auto c = SessionMaintenanceConfig::FromJson(j);
  EXPECT_EQ(c.prune_after_seconds, 86400);
  EXPECT_EQ(c.rotate_bytes, 1048576);
}

// --- Maintenance operations ---

TEST_F(SessionMaintenanceTest, SweepEmptyDir) {
  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.max_entries = 10;
  maint.Configure(config);

  auto result = maint.Sweep(true);
  EXPECT_TRUE(result.swept);
  EXPECT_EQ(result.pruned_count, 0);
}

TEST_F(SessionMaintenanceTest, MaxEntriesEnforced) {
  // Create 5 session files
  for (int i = 0; i < 5; ++i) {
    create_session_file("session" + std::to_string(i) + ".jsonl",
                        R"({"role":"user","content":"test"})");
  }

  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.max_entries = 3;
  maint.Configure(config);

  auto result = maint.Sweep(true);
  EXPECT_TRUE(result.swept);
  EXPECT_EQ(result.pruned_count, 2);

  // Should have 3 files left
  int count = 0;
  for (auto& entry : std::filesystem::directory_iterator(test_dir_)) {
    if (entry.is_regular_file())
      ++count;
  }
  EXPECT_EQ(count, 3);
}

TEST_F(SessionMaintenanceTest, RotateLargeFiles) {
  // Create one large file and one small file
  create_session_file_with_size("large.jsonl", 2000);
  create_session_file_with_size("small.jsonl", 100);

  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.rotate_bytes = 1000;  // Rotate files > 1000 bytes
  maint.Configure(config);

  auto result = maint.Sweep(true);
  EXPECT_TRUE(result.swept);
  EXPECT_EQ(result.rotated_count, 1);
}

TEST_F(SessionMaintenanceTest, WarnModeNoAction) {
  for (int i = 0; i < 5; ++i) {
    create_session_file("session" + std::to_string(i) + ".jsonl",
                        R"({"test": true})");
  }

  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.mode = MaintenanceMode::kWarn;
  config.max_entries = 2;
  maint.Configure(config);

  auto result = maint.Sweep(true);
  EXPECT_TRUE(result.swept);
  EXPECT_EQ(result.pruned_count, 0);  // Warn only, no actual deletion
  EXPECT_FALSE(result.warnings.empty());

  // All files should still exist
  int count = 0;
  for (auto& entry : std::filesystem::directory_iterator(test_dir_)) {
    if (entry.is_regular_file())
      ++count;
  }
  EXPECT_EQ(count, 5);
}

TEST_F(SessionMaintenanceTest, SweepIntervalThrottling) {
  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.sweep_interval_seconds = 3600;  // 1 hour
  config.max_entries = 100;
  maint.Configure(config);

  // First sweep succeeds
  auto r1 = maint.Sweep(true);  // forced
  EXPECT_TRUE(r1.swept);

  // Second sweep without force should be throttled
  auto r2 = maint.Sweep(false);
  EXPECT_FALSE(r2.swept);

  // Forced sweep should work
  auto r3 = maint.Sweep(true);
  EXPECT_TRUE(r3.swept);
}

TEST_F(SessionMaintenanceTest, DiskLimitEnforced) {
  // Create files totaling ~5000 bytes
  for (int i = 0; i < 5; ++i) {
    create_session_file_with_size("session" + std::to_string(i) + ".jsonl",
                                  1000);
  }

  SessionMaintenance maint(test_dir_, make_logger("maint"));
  SessionMaintenanceConfig config;
  config.max_disk_bytes = 3000;  // Only allow 3KB
  maint.Configure(config);

  auto result = maint.Sweep(true);
  EXPECT_TRUE(result.swept);
  EXPECT_GT(result.pruned_count, 0);
}

}  // namespace quantclaw
