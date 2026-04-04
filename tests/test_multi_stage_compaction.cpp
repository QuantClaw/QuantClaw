// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/default_context_engine.hpp"
#include "quantclaw/core/multi_stage_compaction.hpp"

#include <gtest/gtest.h>

namespace quantclaw {

class MultiStageCompactionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
  }

  // Generate a history with N user/assistant message pairs
  std::vector<Message> make_history(int pairs, int chars_per_msg = 100) {
    std::vector<Message> history;
    for (int i = 0; i < pairs; i++) {
      std::string text(static_cast<size_t>(chars_per_msg),
                       static_cast<char>('a' + (i % 26)));
      history.push_back(Message{"user", "Q" + std::to_string(i) + ": " + text});
      history.push_back(
          Message{"assistant", "A" + std::to_string(i) + ": " + text});
    }
    return history;
  }

  std::shared_ptr<spdlog::logger> logger_;
};

// ================================================================
// EstimateTokens
// ================================================================

TEST_F(MultiStageCompactionTest, EstimateTokensEmpty) {
  EXPECT_EQ(MultiStageCompaction::EstimateTokens({}), 0);
}

TEST_F(MultiStageCompactionTest, EstimateTokensBasic) {
  std::vector<Message> msgs;
  msgs.push_back(Message{"user", "hello world"});  // 11 chars ≈ 3 tokens
  int tokens = MultiStageCompaction::EstimateTokens(msgs);
  EXPECT_GT(tokens, 0);
}

// ================================================================
// SplitByTokenShare
// ================================================================

TEST_F(MultiStageCompactionTest, SplitByTokenShareSinglePart) {
  auto history = make_history(5);
  auto chunks = MultiStageCompaction::SplitByTokenShare(history, 1);
  EXPECT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].size(), history.size());
}

TEST_F(MultiStageCompactionTest, SplitByTokenShareTwoParts) {
  auto history = make_history(10);
  auto chunks = MultiStageCompaction::SplitByTokenShare(history, 2);
  EXPECT_EQ(chunks.size(), 2u);

  // All messages accounted for
  size_t total = 0;
  for (const auto& chunk : chunks) {
    total += chunk.size();
  }
  EXPECT_EQ(total, history.size());
}

TEST_F(MultiStageCompactionTest, SplitByTokenShareThreeParts) {
  auto history = make_history(15);
  auto chunks = MultiStageCompaction::SplitByTokenShare(history, 3);
  EXPECT_GE(chunks.size(), 2u);
  EXPECT_LE(chunks.size(), 3u);

  size_t total = 0;
  for (const auto& chunk : chunks) {
    total += chunk.size();
  }
  EXPECT_EQ(total, history.size());
}

TEST_F(MultiStageCompactionTest, SplitByTokenShareEmpty) {
  auto chunks = MultiStageCompaction::SplitByTokenShare({}, 3);
  EXPECT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].empty());
}

// ================================================================
// ChunkByMaxTokens
// ================================================================

TEST_F(MultiStageCompactionTest, ChunkByMaxTokensSmallLimit) {
  auto history = make_history(5, 200);  // ~50 tokens per message
  auto chunks = MultiStageCompaction::ChunkByMaxTokens(history, 100);

  // Should produce multiple chunks
  EXPECT_GT(chunks.size(), 1u);

  // All messages accounted for
  size_t total = 0;
  for (const auto& chunk : chunks) {
    total += chunk.size();
    EXPECT_FALSE(chunk.empty());
  }
  EXPECT_EQ(total, history.size());
}

TEST_F(MultiStageCompactionTest, ChunkByMaxTokensLargeLimit) {
  auto history = make_history(3);
  auto chunks = MultiStageCompaction::ChunkByMaxTokens(history, 100000);
  EXPECT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].size(), history.size());
}

TEST_F(MultiStageCompactionTest, ChunkByMaxTokensEmpty) {
  auto chunks = MultiStageCompaction::ChunkByMaxTokens({}, 100);
  EXPECT_TRUE(chunks.empty());
}

TEST_F(MultiStageCompactionTest, ChunkByMaxTokensZeroLimit) {
  auto history = make_history(3);
  auto chunks = MultiStageCompaction::ChunkByMaxTokens(history, 0);
  EXPECT_EQ(chunks.size(), 1u);
}

// ================================================================
// CompactMultiStage
// ================================================================

TEST_F(MultiStageCompactionTest, SinglePassForSmallHistory) {
  MultiStageCompaction compactor(logger_);
  auto history =
      make_history(3);  // 6 messages, below min_messages_for_multistage

  int summary_calls = 0;
  SummaryFn fn = [&](const std::vector<Message>& msgs) -> std::string {
    summary_calls++;
    return "Summary of " + std::to_string(msgs.size()) + " messages";
  };

  CompactionOptions opts;
  auto result = compactor.CompactMultiStage(history, opts, fn);

  EXPECT_EQ(summary_calls, 1);
  EXPECT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].role, "system");
  EXPECT_NE(result[0].text().find("Summary of"), std::string::npos);
}

TEST_F(MultiStageCompactionTest, MultiStageForLargeHistory) {
  MultiStageCompaction compactor(logger_);
  // 20 pairs = 40 messages, 500 chars each ≈ 125 tokens each
  auto history = make_history(20, 500);

  int summary_calls = 0;
  SummaryFn fn = [&](const std::vector<Message>& msgs) -> std::string {
    summary_calls++;
    return "Chunk summary (" + std::to_string(msgs.size()) + " msgs)";
  };

  CompactionOptions opts;
  opts.max_chunk_tokens = 2000;  // Force multiple chunks
  opts.min_messages_for_multistage = 8;
  auto result = compactor.CompactMultiStage(history, opts, fn);

  // Should have called summary multiple times (once per chunk)
  EXPECT_GT(summary_calls, 1);
  EXPECT_EQ(result.size(), 1u);
  EXPECT_NE(result[0].text().find("stages"), std::string::npos);
}

TEST_F(MultiStageCompactionTest, NoSummaryFnReturnsOriginal) {
  MultiStageCompaction compactor(logger_);
  auto history = make_history(3);

  auto result =
      compactor.CompactMultiStage(history, CompactionOptions{}, nullptr);

  EXPECT_EQ(result.size(), history.size());
}

TEST_F(MultiStageCompactionTest, FinalMergePassWhenExceedsTarget) {
  MultiStageCompaction compactor(logger_);
  auto history = make_history(20, 500);

  int summary_calls = 0;
  SummaryFn fn = [&](const std::vector<Message>& msgs) -> std::string {
    summary_calls++;
    // Return large summaries to trigger final merge
    return std::string(2000, 'x');
  };

  CompactionOptions opts;
  opts.max_chunk_tokens = 2000;
  opts.target_tokens = 100;  // Very small target → force final pass
  opts.safety_margin = 1.0;
  auto result = compactor.CompactMultiStage(history, opts, fn);

  // Extra call for the final merge pass
  EXPECT_GT(summary_calls, 2);
}

// ================================================================
// DefaultContextEngine with MultiStageCompaction
// ================================================================

TEST_F(MultiStageCompactionTest, DefaultEngineUsesMultiStageWithSummaryFn) {
  AgentConfig config;
  config.context_window = 128000;

  DefaultContextEngine engine(config, logger_);

  int summary_calls = 0;
  engine.SetSummaryFn([&](const std::vector<Message>& msgs) -> std::string {
    summary_calls++;
    return "Summarized " + std::to_string(msgs.size()) + " messages";
  });

  auto history = make_history(10);  // 20 messages ≥ 8 threshold
  auto result = engine.CompactOverflow(history, "system prompt", 2);

  EXPECT_GT(summary_calls, 0);  // Multi-stage was used
  EXPECT_EQ(result[0].role, "system");
  EXPECT_EQ(result[0].text(), "system prompt");
}

TEST_F(MultiStageCompactionTest, DefaultEngineFallbackWithoutSummaryFn) {
  AgentConfig config;
  config.context_window = 128000;

  DefaultContextEngine engine(config, logger_);
  // No SetSummaryFn → fallback to truncation

  auto history = make_history(10);
  auto result = engine.CompactOverflow(history, "sys", 3);

  // Should use simple truncation: sys + overflow notice + 3 recent
  EXPECT_EQ(result.size(), 5u);
  EXPECT_EQ(result[0].text(), "sys");
  EXPECT_NE(result[1].text().find("overflow"), std::string::npos);
}

}  // namespace quantclaw
