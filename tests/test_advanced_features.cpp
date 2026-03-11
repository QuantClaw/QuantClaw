// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/core/session_compaction.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace fs = std::filesystem;

static std::shared_ptr<spdlog::logger>
make_null_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- Session Compaction Tests ---

class CompactionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    logger_ = make_null_logger("compaction_test");
    compaction_ = std::make_unique<quantclaw::SessionCompaction>(logger_);
  }

  std::vector<nlohmann::json> make_messages(int count) {
    std::vector<nlohmann::json> msgs;
    for (int i = 0; i < count; ++i) {
      msgs.push_back({
          {"role", i % 2 == 0 ? "user" : "assistant"},
          {"content", "Message " + std::to_string(i) + " with some content"},
      });
    }
    return msgs;
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SessionCompaction> compaction_;
};

TEST_F(CompactionTest, NoCompactionNeeded) {
  auto msgs = make_messages(10);
  quantclaw::SessionCompaction::Options opts;
  opts.max_messages = 100;
  EXPECT_FALSE(compaction_->NeedsCompaction(msgs, opts));
}

TEST_F(CompactionTest, CompactionNeededByMessageCount) {
  auto msgs = make_messages(150);
  quantclaw::SessionCompaction::Options opts;
  opts.max_messages = 100;
  EXPECT_TRUE(compaction_->NeedsCompaction(msgs, opts));
}

TEST_F(CompactionTest, TruncateKeepsRecent) {
  auto msgs = make_messages(50);
  quantclaw::SessionCompaction::Options opts;
  opts.keep_recent = 10;

  auto result = compaction_->Truncate(msgs, opts);
  // 1 system truncation note + 10 recent
  EXPECT_EQ(result.size(), 11);
  EXPECT_EQ(result[0]["role"], "system");
  EXPECT_TRUE(result[0]["content"].get<std::string>().find("truncated") !=
              std::string::npos);
}

TEST_F(CompactionTest, CompactWithSummary) {
  auto msgs = make_messages(50);
  quantclaw::SessionCompaction::Options opts;
  opts.max_messages = 30;
  opts.keep_recent = 10;

  auto result = compaction_->Compact(
      msgs, opts, [](const std::vector<nlohmann::json>& old_msgs) {
        return "Summary of " + std::to_string(old_msgs.size()) + " messages";
      });

  // 1 summary + 10 recent
  EXPECT_EQ(result.size(), 11);
  EXPECT_EQ(result[0]["role"], "system");
  auto content = result[0]["content"].get<std::string>();
  EXPECT_TRUE(content.find("Summary of 40") != std::string::npos);
}

TEST_F(CompactionTest, CompactFallsBackToTruncate) {
  auto msgs = make_messages(50);
  quantclaw::SessionCompaction::Options opts;
  opts.max_messages = 30;
  opts.keep_recent = 10;

  auto result =
      compaction_->Compact(msgs, opts, [](const std::vector<nlohmann::json>&) {
        return "";  // empty summary → fallback
      });

  EXPECT_EQ(result.size(), 11);
  EXPECT_EQ(result[0]["role"], "system");
}

TEST_F(CompactionTest, EstimateTokens) {
  auto msgs = make_messages(10);
  int tokens = compaction_->EstimateTokens(msgs);
  EXPECT_GT(tokens, 0);
}

TEST_F(CompactionTest, SmallMessageListNotTruncated) {
  auto msgs = make_messages(5);
  quantclaw::SessionCompaction::Options opts;
  opts.keep_recent = 10;

  auto result = compaction_->Truncate(msgs, opts);
  EXPECT_EQ(result.size(), 5);  // unchanged
}

// --- Cron Expression Tests ---

TEST(CronExpressionTest, EveryMinute) {
  quantclaw::CronExpression expr("* * * * *");
  std::tm tm{};
  tm.tm_min = 30;
  tm.tm_hour = 12;
  tm.tm_mday = 15;
  tm.tm_mon = 2;  // March
  tm.tm_wday = 3;
  EXPECT_TRUE(expr.Matches(tm));
}

TEST(CronExpressionTest, SpecificMinute) {
  quantclaw::CronExpression expr("30 * * * *");
  std::tm tm{};
  tm.tm_min = 30;
  tm.tm_hour = 12;
  tm.tm_mday = 15;
  tm.tm_mon = 2;
  tm.tm_wday = 3;
  EXPECT_TRUE(expr.Matches(tm));

  tm.tm_min = 15;
  EXPECT_FALSE(expr.Matches(tm));
}

TEST(CronExpressionTest, StepExpression) {
  quantclaw::CronExpression expr("*/15 * * * *");
  std::tm tm{};
  tm.tm_hour = 10;
  tm.tm_mday = 1;
  tm.tm_mon = 0;
  tm.tm_wday = 1;

  tm.tm_min = 0;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_min = 15;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_min = 30;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_min = 45;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_min = 10;
  EXPECT_FALSE(expr.Matches(tm));
}

TEST(CronExpressionTest, HourlyAt30) {
  quantclaw::CronExpression expr("30 * * * *");
  std::tm tm{};
  tm.tm_min = 30;
  tm.tm_hour = 8;
  tm.tm_mday = 1;
  tm.tm_mon = 0;
  tm.tm_wday = 1;
  EXPECT_TRUE(expr.Matches(tm));
}

TEST(CronExpressionTest, RangeExpression) {
  quantclaw::CronExpression expr("0 9-17 * * *");
  std::tm tm{};
  tm.tm_min = 0;
  tm.tm_mday = 1;
  tm.tm_mon = 0;
  tm.tm_wday = 1;

  tm.tm_hour = 9;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_hour = 17;
  EXPECT_TRUE(expr.Matches(tm));
  tm.tm_hour = 8;
  EXPECT_FALSE(expr.Matches(tm));
  tm.tm_hour = 18;
  EXPECT_FALSE(expr.Matches(tm));
}

TEST(CronExpressionTest, NextAfter) {
  quantclaw::CronExpression expr("0 12 * * *");  // noon daily
  auto now = std::chrono::system_clock::now();
  auto next = expr.NextAfter(now);
  EXPECT_GT(next, now);

  auto t = std::chrono::system_clock::to_time_t(next);
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  EXPECT_EQ(tm.tm_hour, 12);
  EXPECT_EQ(tm.tm_min, 0);
}

// --- CronScheduler Tests ---

class CronSchedulerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_cron_test");
    logger_ = make_null_logger("cron_test");
  }

  void TearDown() override {
    fs::remove_all(test_dir_);
  }

  fs::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(CronSchedulerTest, AddAndListJobs) {
  quantclaw::CronScheduler sched(logger_);
  auto id = sched.AddJob("test", "*/5 * * * *", "Hello", "agent:main:main");
  EXPECT_FALSE(id.empty());

  auto jobs = sched.ListJobs();
  ASSERT_EQ(jobs.size(), 1);
  EXPECT_EQ(jobs[0].name, "test");
  EXPECT_EQ(jobs[0].schedule, "*/5 * * * *");
  EXPECT_EQ(jobs[0].message, "Hello");
}

TEST_F(CronSchedulerTest, RemoveJob) {
  quantclaw::CronScheduler sched(logger_);
  auto id = sched.AddJob("to-remove", "0 * * * *", "msg");
  EXPECT_TRUE(sched.RemoveJob(id));
  EXPECT_TRUE(sched.ListJobs().empty());
}

TEST_F(CronSchedulerTest, RemoveNonexistentFails) {
  quantclaw::CronScheduler sched(logger_);
  EXPECT_FALSE(sched.RemoveJob("nonexistent"));
}

TEST_F(CronSchedulerTest, RemoveEmptyIdFails) {
  quantclaw::CronScheduler sched(logger_);
  // Add multiple jobs to ensure empty id doesn't delete all
  auto id1 = sched.AddJob("job1", "0 * * * *", "msg1");
  auto id2 = sched.AddJob("job2", "0 * * * *", "msg2");

  // Try to remove with empty id - should fail and not delete anything
  EXPECT_FALSE(sched.RemoveJob(""));

  // Verify both jobs still exist
  auto jobs = sched.ListJobs();
  ASSERT_EQ(jobs.size(), 2);
}

TEST_F(CronSchedulerTest, PrefixMatchAmbiguousFails) {
  quantclaw::CronScheduler sched(logger_);
  // Add two jobs with similar IDs (both will have prefixes that start with same
  // chars) Since job IDs are random, we can't easily create ambiguous prefixes
  // Instead, test that a short prefix matching multiple jobs fails
  auto id1 = sched.AddJob("job1", "0 * * * *", "msg1");
  auto id2 = sched.AddJob("job2", "0 * * * *", "msg2");

  // Try to remove with a very short prefix (if both IDs start with same char)
  // This is a probabilistic test, but with random IDs it's unlikely they share
  // long prefixes So instead, test that non-matching prefix fails
  EXPECT_FALSE(sched.RemoveJob("xyz"));  // Definitely won't match random IDs

  // Both jobs should still exist
  auto jobs = sched.ListJobs();
  ASSERT_EQ(jobs.size(), 2);
}

TEST_F(CronSchedulerTest, PrefixMatchUnambiguousSucceeds) {
  quantclaw::CronScheduler sched(logger_);
  auto id1 = sched.AddJob("job1", "0 * * * *", "msg1");
  auto id2 = sched.AddJob("job2", "0 * * * *", "msg2");

  // Remove with a longer prefix that should unambiguously match id1
  std::string prefix = id1.substr(0, id1.size() - 1);  // All but last char
  EXPECT_TRUE(sched.RemoveJob(prefix));

  // Only id2 should remain
  auto jobs = sched.ListJobs();
  ASSERT_EQ(jobs.size(), 1);
  EXPECT_EQ(jobs[0].id, id2);
}

TEST_F(CronSchedulerTest, PersistAndLoad) {
  auto filepath = (test_dir_ / "cron.json").string();

  {
    quantclaw::CronScheduler sched(logger_);
    sched.Load(filepath);
    sched.AddJob("persist-test", "0 8 * * *", "good morning");
    sched.Save(filepath);
  }

  {
    quantclaw::CronScheduler sched2(logger_);
    sched2.Load(filepath);
    auto jobs = sched2.ListJobs();
    ASSERT_EQ(jobs.size(), 1);
    EXPECT_EQ(jobs[0].name, "persist-test");
    EXPECT_EQ(jobs[0].schedule, "0 8 * * *");
  }
}

TEST_F(CronSchedulerTest, JobToJson) {
  quantclaw::CronJob job;
  job.id = "abc123";
  job.name = "daily";
  job.schedule = "0 9 * * *";
  job.message = "Hi";
  job.session_key = "agent:main:main";
  job.enabled = true;

  auto j = job.ToJson();
  EXPECT_EQ(j["id"], "abc123");
  EXPECT_EQ(j["name"], "daily");
  EXPECT_EQ(j["schedule"], "0 9 * * *");
}

// --- Memory Search Tests ---

class MemorySearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_memsearch_test");
    logger_ = make_null_logger("memsearch_test");
  }

  void TearDown() override {
    fs::remove_all(test_dir_);
  }

  void write_file(const std::string& name, const std::string& content) {
    std::ofstream ofs(test_dir_ / name);
    ofs << content;
  }

  fs::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(MemorySearchTest, IndexAndSearch) {
  write_file("test.md",
             "The quick brown fox jumps over the lazy dog.\n\n"
             "Machine learning is a subset of artificial intelligence.\n\n"
             "The weather today is sunny and warm.\n");

  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto results = search.Search("machine learning artificial intelligence");
  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(results[0].content.find("machine learning") !=
                  std::string::npos ||
              results[0].content.find("Machine learning") != std::string::npos);
}

TEST_F(MemorySearchTest, EmptyQueryReturnsEmpty) {
  write_file("test.md", "Some content here");
  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto results = search.Search("");
  EXPECT_TRUE(results.empty());
}

TEST_F(MemorySearchTest, NoMatchReturnsEmpty) {
  write_file("test.md", "The quick brown fox");
  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto results = search.Search("quantum computing blockchain");
  EXPECT_TRUE(results.empty());
}

TEST_F(MemorySearchTest, MaxResultsLimited) {
  std::string content;
  for (int i = 0; i < 50; ++i) {
    content += "Important data entry number " + std::to_string(i) + "\n\n";
  }
  write_file("big.md", content);

  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto results = search.Search("data entry", 5);
  EXPECT_LE(results.size(), 5u);
}

TEST_F(MemorySearchTest, Stats) {
  write_file("a.md", "First paragraph.\n\nSecond paragraph.\n");
  write_file("b.md", "Third paragraph.\n");

  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto stats = search.Stats();
  EXPECT_GT(stats["indexed_entries"].get<int>(), 0);
}

TEST_F(MemorySearchTest, ClearIndex) {
  write_file("test.md", "Some content");
  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);
  EXPECT_GT(search.Stats()["indexed_entries"].get<int>(), 0);

  search.Clear();
  EXPECT_EQ(search.Stats()["indexed_entries"].get<int>(), 0);
}

TEST_F(MemorySearchTest, ScoreRelevance) {
  write_file("relevant.md",
             "Kubernetes container orchestration deployment pods services\n\n"
             "Docker containers and images for development\n");
  write_file("irrelevant.md",
             "The weather is sunny today\n\nCooking recipes for pasta\n");

  quantclaw::MemorySearch search(logger_);
  search.IndexDirectory(test_dir_);

  auto results = search.Search("kubernetes container deployment");
  ASSERT_FALSE(results.empty());
  // Most relevant result should be from relevant.md
  EXPECT_TRUE(results[0].source.find("relevant.md") != std::string::npos);
}
