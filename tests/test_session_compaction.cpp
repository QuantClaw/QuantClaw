// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/session_compaction.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;
using json = nlohmann::json;

static json make_msg(const std::string& role, const std::string& content) {
  return {{"role", role}, {"content", content}};
}

class SessionCompactionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
    compaction_ = std::make_unique<SessionCompaction>(logger_);
  }
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<SessionCompaction> compaction_;
};

// 1. NeedsCompaction returns false when under both thresholds
TEST_F(SessionCompactionTest, NeedsCompaction_BelowThresholds) {
  std::vector<json> messages;
  for (int i = 0; i < 5; ++i) {
    messages.push_back(make_msg("user", "Short message"));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 100;
  opts.max_tokens = 100000;
  EXPECT_FALSE(compaction_->NeedsCompaction(messages, opts));
}

// 2. NeedsCompaction returns true when exceeding max_messages
TEST_F(SessionCompactionTest, NeedsCompaction_ExceedsMessageCount) {
  std::vector<json> messages;
  for (int i = 0; i < 10; ++i) {
    messages.push_back(make_msg("user", "msg"));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 5;
  opts.max_tokens = 100000;
  EXPECT_TRUE(compaction_->NeedsCompaction(messages, opts));
}

// 3. NeedsCompaction returns true when total tokens > max_tokens
TEST_F(SessionCompactionTest, NeedsCompaction_ExceedsTokens) {
  std::vector<json> messages;
  // Each message has 400 chars = 100 tokens, 20 messages = 2000 tokens
  std::string big_content(400, 'x');
  for (int i = 0; i < 20; ++i) {
    messages.push_back(make_msg("user", big_content));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10000;  // Don't trigger on count
  opts.max_tokens = 1000;     // 2000 > 1000
  EXPECT_TRUE(compaction_->NeedsCompaction(messages, opts));
}

// 4. Compact returns original messages unchanged when below threshold
TEST_F(SessionCompactionTest, Compact_NoActionWhenBelowThreshold) {
  std::vector<json> messages;
  for (int i = 0; i < 3; ++i) {
    messages.push_back(make_msg("user", "Hello " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 100;
  opts.max_tokens = 100000;

  auto result = compaction_->Compact(messages, opts, nullptr);
  EXPECT_EQ(result.size(), messages.size());
  for (size_t i = 0; i < messages.size(); ++i) {
    EXPECT_EQ(result[i]["content"], messages[i]["content"]);
  }
}

// 5. Compact summarizes older messages, result has system summary + recent
TEST_F(SessionCompactionTest, Compact_SummarizesOlderMessages) {
  std::vector<json> messages;
  for (int i = 0; i < 30; ++i) {
    messages.push_back(make_msg("user", "Message number " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10;
  opts.keep_recent = 5;
  opts.max_tokens = 100000;

  bool summary_fn_called = false;
  std::vector<json> summarized_msgs;
  auto summary_fn = [&](const std::vector<json>& old_msgs) -> std::string {
    summary_fn_called = true;
    summarized_msgs = old_msgs;
    return "This is a summary of older messages.";
  };

  auto result = compaction_->Compact(messages, opts, summary_fn);
  EXPECT_TRUE(summary_fn_called);
  // summary_fn should have been called with the 25 oldest messages
  EXPECT_EQ(static_cast<int>(summarized_msgs.size()), 25);
  // Result should be: 1 system summary + 5 recent = 6 messages
  EXPECT_EQ(result.size(), 6u);
  EXPECT_EQ(result[0]["role"], "system");
  EXPECT_TRUE(result[0]["content"].get<std::string>().find("summary") !=
              std::string::npos);
}

// 6. Compact falls back to Truncate when summary_fn throws
TEST_F(SessionCompactionTest, Compact_FallbackToTruncateOnSummaryFailure) {
  std::vector<json> messages;
  for (int i = 0; i < 30; ++i) {
    messages.push_back(make_msg("user", "msg " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10;
  opts.keep_recent = 5;

  auto bad_fn = [](const std::vector<json>&) -> std::string {
    throw std::runtime_error("LLM unavailable");
  };

  auto result = compaction_->Compact(messages, opts, bad_fn);
  // Should have fallen back to Truncate: 1 system note + 5 recent = 6
  EXPECT_EQ(result.size(), 6u);
  EXPECT_EQ(result[0]["role"], "system");
  EXPECT_TRUE(result[0]["content"].get<std::string>().find("truncated") !=
              std::string::npos);
}

// 7. Compact falls back to Truncate when summary_fn returns ""
TEST_F(SessionCompactionTest, Compact_FallbackWhenSummaryEmpty) {
  std::vector<json> messages;
  for (int i = 0; i < 30; ++i) {
    messages.push_back(make_msg("user", "msg " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10;
  opts.keep_recent = 5;

  auto empty_fn = [](const std::vector<json>&) -> std::string { return ""; };

  auto result = compaction_->Compact(messages, opts, empty_fn);
  // Falls back to truncation
  EXPECT_EQ(result.size(), 6u);
  EXPECT_EQ(result[0]["role"], "system");
  EXPECT_TRUE(result[0]["content"].get<std::string>().find("truncated") !=
              std::string::npos);
}

// 8. Truncate keeps only keep_recent newest messages
TEST_F(SessionCompactionTest, Truncate_KeepsRecentMessages) {
  std::vector<json> messages;
  for (int i = 0; i < 20; ++i) {
    messages.push_back(make_msg("user", "Message " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.keep_recent = 5;

  auto result = compaction_->Truncate(messages, opts);
  // 1 system note + 5 recent = 6
  EXPECT_EQ(result.size(), 6u);
  // The 5 recent messages should be messages 15-19
  for (size_t i = 1; i <= 5; ++i) {
    EXPECT_EQ(result[i]["content"], "Message " + std::to_string(15 + i - 1));
  }
}

// 9. Truncate prepends a system note about removed messages
TEST_F(SessionCompactionTest, Truncate_PrependsSystemNote) {
  std::vector<json> messages;
  for (int i = 0; i < 20; ++i) {
    messages.push_back(make_msg("user", "msg"));
  }
  SessionCompaction::Options opts;
  opts.keep_recent = 5;

  auto result = compaction_->Truncate(messages, opts);
  EXPECT_EQ(result[0]["role"], "system");
  auto note = result[0]["content"].get<std::string>();
  EXPECT_TRUE(note.find("15") != std::string::npos);
  EXPECT_TRUE(note.find("removed") != std::string::npos);
}

// 10. Truncate is a no-op when total <= keep_recent
TEST_F(SessionCompactionTest, Truncate_NoOpWhenBelowKeepRecent) {
  std::vector<json> messages;
  for (int i = 0; i < 3; ++i) {
    messages.push_back(make_msg("user", "msg " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.keep_recent = 10;

  auto result = compaction_->Truncate(messages, opts);
  EXPECT_EQ(result.size(), messages.size());
}

// 11. EstimateTokens: 400 chars / 4 = 100 tokens
TEST_F(SessionCompactionTest, EstimateTokens_CalculatesCorrectly) {
  std::vector<json> messages;
  messages.push_back(make_msg("user", std::string(400, 'a')));
  EXPECT_EQ(compaction_->EstimateTokens(messages), 100);
}

// 12. EstimateTokens: messages without string content counted as 0
TEST_F(SessionCompactionTest, EstimateTokens_SkipsNonStringContent) {
  std::vector<json> messages;
  // Message with array content (not a string)
  messages.push_back({{"role", "user"}, {"content", json::array({1, 2, 3})}});
  // Message with no content key at all
  messages.push_back({{"role", "system"}});
  EXPECT_EQ(compaction_->EstimateTokens(messages), 0);
}

// 13. Compact preserves message order
TEST_F(SessionCompactionTest, Compact_PreservesMessageOrder) {
  std::vector<json> messages;
  for (int i = 0; i < 30; ++i) {
    messages.push_back(make_msg("user", "Ordered_" + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10;
  opts.keep_recent = 5;

  auto summary_fn = [](const std::vector<json>&) -> std::string {
    return "Summary of old messages.";
  };

  auto result = compaction_->Compact(messages, opts, summary_fn);
  // result[0] is summary, result[1..5] are messages 25-29
  for (size_t i = 1; i < result.size(); ++i) {
    size_t expected_idx = 25 + i - 1;
    EXPECT_EQ(result[i]["content"], "Ordered_" + std::to_string(expected_idx));
  }
}

// 14. Compact summary message has compacted=true and summarized_count
TEST_F(SessionCompactionTest, Compact_SummaryContainsMetadata) {
  std::vector<json> messages;
  for (int i = 0; i < 30; ++i) {
    messages.push_back(make_msg("user", "msg " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 10;
  opts.keep_recent = 5;

  auto summary_fn = [](const std::vector<json>&) -> std::string {
    return "Summary.";
  };

  auto result = compaction_->Compact(messages, opts, summary_fn);
  ASSERT_TRUE(result[0].contains("metadata"));
  EXPECT_TRUE(result[0]["metadata"]["compacted"].get<bool>());
  EXPECT_EQ(result[0]["metadata"]["summarized_count"], 25);
}

// 15. Truncate metadata has truncated=true and removed_count
TEST_F(SessionCompactionTest, Truncate_MetadataContainsRemovedCount) {
  std::vector<json> messages;
  for (int i = 0; i < 20; ++i) {
    messages.push_back(make_msg("user", "msg"));
  }
  SessionCompaction::Options opts;
  opts.keep_recent = 5;

  auto result = compaction_->Truncate(messages, opts);
  ASSERT_TRUE(result[0].contains("metadata"));
  EXPECT_TRUE(result[0]["metadata"]["truncated"].get<bool>());
  EXPECT_EQ(result[0]["metadata"]["removed_count"], 15);
}

// 16. Compact: when keep_recent > total messages, keeps all
TEST_F(SessionCompactionTest, Compact_KeepRecentClampedToTotal) {
  std::vector<json> messages;
  for (int i = 0; i < 5; ++i) {
    messages.push_back(make_msg("user", "msg " + std::to_string(i)));
  }
  SessionCompaction::Options opts;
  opts.max_messages = 2;   // Trigger compaction
  opts.keep_recent = 100;  // More than total
  opts.max_tokens = 100000;

  auto summary_fn = [](const std::vector<json>&) -> std::string {
    return "Should not be called.";
  };

  auto result = compaction_->Compact(messages, opts, summary_fn);
  // keep_recent (100) > total (5), so to_summarize = 0, returns original
  EXPECT_EQ(result.size(), messages.size());
}
