// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Tests for P1 features:
// - Usage accumulator (#16)
// - Dynamic max iterations (#17)
// - Context window guard (#15)
// - Tool result truncation (#14)
// - Overflow compaction retry (#13)
// - Budget-based context pruning (#26)

#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/constants.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/context_pruner.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/usage_accumulator.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/providers/provider_error.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

using namespace quantclaw;

// =============================================================================
// P1 #16: Usage Accumulator
// =============================================================================

TEST(UsageAccumulatorTest, RecordAndRetrieve) {
  UsageAccumulator acc;
  acc.Record("session-1", 100, 50);

  auto stats = acc.GetSession("session-1");
  EXPECT_EQ(stats.input_tokens, 100);
  EXPECT_EQ(stats.output_tokens, 50);
  EXPECT_EQ(stats.total_tokens, 150);
  EXPECT_EQ(stats.turns, 1);
}

TEST(UsageAccumulatorTest, AccumulatesMultipleRecords) {
  UsageAccumulator acc;
  acc.Record("s1", 100, 50);
  acc.Record("s1", 200, 100);

  auto stats = acc.GetSession("s1");
  EXPECT_EQ(stats.input_tokens, 300);
  EXPECT_EQ(stats.output_tokens, 150);
  EXPECT_EQ(stats.total_tokens, 450);
  EXPECT_EQ(stats.turns, 2);
}

TEST(UsageAccumulatorTest, TrackGlobally) {
  UsageAccumulator acc;
  acc.Record("s1", 100, 50);
  acc.Record("s2", 200, 100);

  auto global = acc.GetGlobal();
  EXPECT_EQ(global.input_tokens, 300);
  EXPECT_EQ(global.output_tokens, 150);
  EXPECT_EQ(global.total_tokens, 450);
  EXPECT_EQ(global.turns, 2);
}

TEST(UsageAccumulatorTest, ResetSession) {
  UsageAccumulator acc;
  acc.Record("s1", 100, 50);
  acc.Record("s2", 200, 100);

  acc.ResetSession("s1");
  auto s1 = acc.GetSession("s1");
  EXPECT_EQ(s1.turns, 0);

  // Global still has both
  auto global = acc.GetGlobal();
  EXPECT_EQ(global.turns, 2);
}

TEST(UsageAccumulatorTest, ResetAll) {
  UsageAccumulator acc;
  acc.Record("s1", 100, 50);
  acc.ResetAll();

  auto global = acc.GetGlobal();
  EXPECT_EQ(global.turns, 0);
  EXPECT_EQ(global.total_tokens, 0);
}

TEST(UsageAccumulatorTest, NonexistentSessionReturnsZero) {
  UsageAccumulator acc;
  auto stats = acc.GetSession("nonexistent");
  EXPECT_EQ(stats.turns, 0);
  EXPECT_EQ(stats.total_tokens, 0);
}

TEST(UsageAccumulatorTest, ToJsonFormat) {
  UsageAccumulator acc;
  acc.Record("s1", 100, 50);
  acc.Record("s2", 200, 100);

  auto j = acc.ToJson();
  EXPECT_TRUE(j.contains("global"));
  EXPECT_TRUE(j.contains("sessions"));
  EXPECT_EQ(j["global"]["totalTokens"], 450);
  EXPECT_EQ(j["sessions"]["s1"]["inputTokens"], 100);
  EXPECT_EQ(j["sessions"]["s2"]["outputTokens"], 100);
}

// =============================================================================
// P1 #17: Dynamic Max Iterations
// =============================================================================

TEST(DynamicIterationsTest, SmallContextWindow) {
  AgentConfig config;
  config.context_window = 16384;                                // 16K
  EXPECT_EQ(config.DynamicMaxIterations(), kMinMaxIterations);  // 32
}

TEST(DynamicIterationsTest, DefaultContextWindow) {
  AgentConfig config;
  config.context_window = kDefaultContextWindow;  // 128K
  int iters = config.DynamicMaxIterations();
  EXPECT_GE(iters, kMinMaxIterations);
  EXPECT_LE(iters, kMaxMaxIterations);
}

TEST(DynamicIterationsTest, LargeContextWindow) {
  AgentConfig config;
  config.context_window = kContextWindow200K;
  EXPECT_EQ(config.DynamicMaxIterations(), kMaxMaxIterations);  // 160
}

TEST(DynamicIterationsTest, ScalesLinearly) {
  AgentConfig config;
  // Midpoint: ~116K context
  config.context_window = (kContextWindow32K + kContextWindow200K) / 2;
  int mid = config.DynamicMaxIterations();
  // Should be roughly in the middle of 32-160
  EXPECT_GE(mid, 80);
  EXPECT_LE(mid, 110);
}

TEST(DynamicIterationsTest, ConstantsValid) {
  EXPECT_EQ(kMinMaxIterations, 32);
  EXPECT_EQ(kMaxMaxIterations, 160);
  EXPECT_EQ(kDefaultMaxIterations, 32);
}

// =============================================================================
// P1 #15: Context Window Guard
// =============================================================================

TEST(ContextWindowGuardTest, ConstantsPresent) {
  EXPECT_EQ(kContextWindowMinTokens, 16384);
  EXPECT_EQ(kDefaultContextWindow, 128000);
}

// =============================================================================
// P1 #14: Tool Result Truncation
// =============================================================================

TEST(ToolResultTruncationTest, ConstantsPresent) {
  EXPECT_EQ(kToolResultMaxChars, 30000);
  EXPECT_EQ(kToolResultKeepLines, 20);
}

// =============================================================================
// P1 #13: Overflow Compaction Retry
// =============================================================================

TEST(OverflowRetryTest, ConstantsPresent) {
  EXPECT_EQ(kOverflowCompactionMaxRetries, 3);
}

TEST(ContextOverflowTest, ClassifyContextOverflow) {
  // Various patterns that should be recognized as context overflow
  EXPECT_EQ(ClassifyHttpError(400, "context_length exceeded"),
            ProviderErrorKind::kContextOverflow);
  EXPECT_EQ(ClassifyHttpError(400, "maximum context length is 128000"),
            ProviderErrorKind::kContextOverflow);
  EXPECT_EQ(ClassifyHttpError(400, "context_window exceeded"),
            ProviderErrorKind::kContextOverflow);
  EXPECT_EQ(ClassifyHttpError(400, "token limit exceeded"),
            ProviderErrorKind::kContextOverflow);
}

TEST(ContextOverflowTest, ContextOverflowToString) {
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kContextOverflow),
            "context_overflow");
}

// Mock provider for overflow testing
class OverflowMockProvider : public LLMProvider {
 public:
  int call_count = 0;
  int fail_until = 0;  // Throw context overflow for first N calls

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override {
    call_count++;
    if (call_count <= fail_until) {
      throw ProviderError(ProviderErrorKind::kContextOverflow, 400,
                          "context_length exceeded");
    }
    ChatCompletionResponse resp;
    resp.content = "Recovery response after overflow";
    resp.finish_reason = "stop";
    resp.usage = {static_cast<int>(request.messages.size()) * 10, 20, 0};
    return resp;
  }

  void ChatCompletionStream(
      const ChatCompletionRequest&,
      std::function<void(const ChatCompletionResponse&)> callback) override {
    ChatCompletionResponse resp;
    resp.content = "streamed";
    resp.is_stream_end = true;
    callback(resp);
  }

  std::string GetProviderName() const override {
    return "overflow-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock"};
  }
};

class P1AgentLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_p1_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    memory_manager_ = std::make_shared<MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<SkillLoader>(logger_);
    tool_registry_ = std::make_shared<ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
};

TEST_F(P1AgentLoopTest, OverflowCompactionRetryRecovers) {
  auto mock = std::make_shared<OverflowMockProvider>();
  mock->fail_until = 2;  // Fail first 2 calls, succeed on 3rd

  AgentConfig config;
  config.model = "test-model";
  config.context_window = 128000;

  auto loop = std::make_unique<AgentLoop>(
      memory_manager_, skill_loader_, tool_registry_, mock, config, logger_);

  auto msgs = loop->ProcessMessage("Hello", {}, "System prompt");
  ASSERT_FALSE(msgs.empty());
  EXPECT_EQ(msgs.back().content[0].text, "Recovery response after overflow");
  EXPECT_EQ(mock->call_count, 3);
}

TEST_F(P1AgentLoopTest, OverflowCompactionRetryExhausted) {
  auto mock = std::make_shared<OverflowMockProvider>();
  mock->fail_until = 100;  // Always fail

  AgentConfig config;
  config.model = "test-model";
  config.context_window = 128000;

  auto loop = std::make_unique<AgentLoop>(
      memory_manager_, skill_loader_, tool_registry_, mock, config, logger_);

  // After 3 retries, should throw
  EXPECT_THROW(loop->ProcessMessage("Hello", {}, "System"), ProviderError);
  EXPECT_EQ(mock->call_count, 4);  // 1 initial + 3 retries
}

// Usage accumulator integration test
class UsageMockProvider : public LLMProvider {
 public:
  ChatCompletionResponse ChatCompletion(const ChatCompletionRequest&) override {
    ChatCompletionResponse resp;
    resp.content = "Response";
    resp.finish_reason = "stop";
    resp.usage = {100, 50, 150};
    return resp;
  }

  void ChatCompletionStream(
      const ChatCompletionRequest&,
      std::function<void(const ChatCompletionResponse&)> callback) override {
    ChatCompletionResponse resp;
    resp.content = "streamed";
    resp.is_stream_end = true;
    resp.usage = {200, 100, 300};
    callback(resp);
  }

  std::string GetProviderName() const override {
    return "usage-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock"};
  }
};

TEST_F(P1AgentLoopTest, UsageAccumulatorTracksTokens) {
  auto mock = std::make_shared<UsageMockProvider>();
  auto acc = std::make_shared<UsageAccumulator>();

  AgentConfig config;
  config.model = "test-model";
  config.context_window = 128000;

  auto loop = std::make_unique<AgentLoop>(
      memory_manager_, skill_loader_, tool_registry_, mock, config, logger_);
  loop->SetSessionKey("test-session");
  loop->SetUsageAccumulator(acc);

  loop->ProcessMessage("Hello", {}, "System");

  auto stats = acc->GetSession("test-session");
  EXPECT_EQ(stats.input_tokens, 100);
  EXPECT_EQ(stats.output_tokens, 50);
  EXPECT_EQ(stats.turns, 1);
}

TEST_F(P1AgentLoopTest, UsageAccumulatorTracksStreaming) {
  auto mock = std::make_shared<UsageMockProvider>();
  auto acc = std::make_shared<UsageAccumulator>();

  AgentConfig config;
  config.model = "test-model";
  config.context_window = 128000;

  auto loop = std::make_unique<AgentLoop>(
      memory_manager_, skill_loader_, tool_registry_, mock, config, logger_);
  loop->SetSessionKey("test-stream");
  loop->SetUsageAccumulator(acc);

  loop->ProcessMessageStream("Hello", {}, "System", nullptr);

  auto stats = acc->GetSession("test-stream");
  EXPECT_EQ(stats.input_tokens, 200);
  EXPECT_EQ(stats.output_tokens, 100);
  EXPECT_EQ(stats.turns, 1);
}

TEST_F(P1AgentLoopTest, DynamicMaxIterationsUsed) {
  auto mock = std::make_shared<UsageMockProvider>();

  AgentConfig config;
  config.model = "test-model";
  config.context_window = kContextWindow200K;  // Should give 160 iterations

  auto loop = std::make_unique<AgentLoop>(
      memory_manager_, skill_loader_, tool_registry_, mock, config, logger_);

  // The loop should use dynamic iterations
  // We can't directly inspect max_iterations_, but we can verify
  // SetConfig updates it correctly
  AgentConfig new_config = config;
  new_config.context_window = kContextWindow32K;  // Should give 32 iterations
  loop->SetConfig(new_config);
  // If it was using dynamic, it should accept a message fine
  auto msgs = loop->ProcessMessage("test", {}, "sys");
  ASSERT_FALSE(msgs.empty());
}

// =============================================================================
// P1 #26: Budget-based Context Pruning
// =============================================================================

TEST(BudgetPruningTest, SmallContextTriggersAggressivePrune) {
  // Create history with many large tool results
  std::vector<Message> history;
  history.push_back(Message{"system", "You are a helpful assistant."});
  history.push_back(Message{"user", "Read the files"});

  // Add 20 assistant+tool_result pairs with large results
  for (int i = 0; i < 20; ++i) {
    Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(
        ContentBlock::MakeToolUse("t" + std::to_string(i), "read",
                                  {{"path", "/tmp/file" + std::to_string(i)}}));
    history.push_back(assistant);

    Message result;
    result.role = "user";
    result.content.push_back(ContentBlock::MakeToolResult(
        "t" + std::to_string(i), std::string(5000, 'x')));  // 5000 chars each
    history.push_back(result);
  }

  // With budget-based pruning (small window), should prune aggressively
  ContextPruner::Options opts;
  opts.context_window = 32768;  // Small context window
  opts.max_tokens = 8192;
  opts.prune_target_ratio = 0.75;

  auto pruned = ContextPruner::Prune(history, opts);

  // Pruned should have same number of messages but smaller content
  EXPECT_EQ(pruned.size(), history.size());

  int original_tokens = ContextPruner::EstimateTokens(history);
  int pruned_tokens = ContextPruner::EstimateTokens(pruned);
  EXPECT_LT(pruned_tokens, original_tokens);
}

TEST(BudgetPruningTest, LargeContextKeepsMore) {
  std::vector<Message> history;
  history.push_back(Message{"user", "Start"});

  for (int i = 0; i < 5; ++i) {
    Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(ContentBlock::MakeToolUse(
        "t" + std::to_string(i), "read", {{"path", "/tmp/f"}}));
    history.push_back(assistant);

    Message result;
    result.role = "user";
    result.content.push_back(ContentBlock::MakeToolResult(
        "t" + std::to_string(i), std::string(3000, 'y')));
    history.push_back(result);
  }

  // With large context window, should keep most content
  ContextPruner::Options opts_large;
  opts_large.context_window = 200000;
  opts_large.max_tokens = 8192;

  auto pruned_large = ContextPruner::Prune(history, opts_large);
  int large_tokens = ContextPruner::EstimateTokens(pruned_large);

  // With no budget (legacy mode), should behave similarly
  ContextPruner::Options opts_legacy;
  opts_legacy.context_window = 0;

  auto pruned_legacy = ContextPruner::Prune(history, opts_legacy);
  int legacy_tokens = ContextPruner::EstimateTokens(pruned_legacy);

  // Both should be similar since there's enough budget
  EXPECT_EQ(large_tokens, legacy_tokens);
}

TEST(BudgetPruningTest, EstimateTokensWorks) {
  Message msg{"user", "Hello world"};
  int tokens = ContextPruner::EstimateTokens(msg);
  EXPECT_GT(tokens, 0);

  std::vector<Message> msgs = {msg, msg};
  int total = ContextPruner::EstimateTokens(msgs);
  EXPECT_EQ(total, tokens * 2);
}

TEST(BudgetPruningTest, NoBudgetUsesLegacyBehavior) {
  std::vector<Message> history;
  history.push_back(Message{"user", "test"});

  ContextPruner::Options opts;
  opts.context_window = 0;  // Disabled

  auto pruned = ContextPruner::Prune(history, opts);
  EXPECT_EQ(pruned.size(), history.size());
}

// =============================================================================
// TokenUsage in ChatCompletionResponse
// =============================================================================

TEST(TokenUsageTest, DefaultZero) {
  TokenUsage usage;
  EXPECT_EQ(usage.prompt_tokens, 0);
  EXPECT_EQ(usage.completion_tokens, 0);
  EXPECT_EQ(usage.total_tokens, 0);
}

TEST(TokenUsageTest, InResponse) {
  ChatCompletionResponse resp;
  resp.usage.prompt_tokens = 100;
  resp.usage.completion_tokens = 50;
  resp.usage.total_tokens = 150;
  EXPECT_EQ(resp.usage.prompt_tokens, 100);
}
