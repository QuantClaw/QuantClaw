// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

import std;
import quantclaw.config;
import quantclaw.providers.llm_provider;
import quantclaw.core.context_engine;
import quantclaw.core.default_context_engine;

namespace quantclaw {

// ================================================================
// Custom ContextEngine for injection testing
// ================================================================

class TestContextEngine : public ContextEngine {
 public:
  std::string Name() const override {
    return "test-engine";
  }

  AssembleResult Assemble(const std::vector<Message>& history,
                          const std::string& system_prompt,
                          const std::string& user_message, int context_window,
                          int max_tokens) override {
    assemble_called = true;
    last_history_size = static_cast<int>(history.size());
    last_system_prompt = system_prompt;
    last_user_message = user_message;

    AssembleResult result;
    if (!system_prompt.empty()) {
      result.messages.push_back(Message{"system", system_prompt});
    }
    for (const auto& m : history) {
      result.messages.push_back(m);
    }
    result.messages.push_back(Message{"user", user_message});
    result.estimated_tokens = 100;
    return result;
  }

  std::vector<Message> CompactOverflow(const std::vector<Message>& messages,
                                       const std::string& system_prompt,
                                       int keep_recent) override {
    compact_called = true;
    // Keep only last 2 messages
    std::vector<Message> result;
    result.push_back(Message{"system", "compacted"});
    if (messages.size() >= 2) {
      result.push_back(messages[messages.size() - 2]);
      result.push_back(messages[messages.size() - 1]);
    }
    return result;
  }

  bool assemble_called = false;
  bool compact_called = false;
  int last_history_size = 0;
  std::string last_system_prompt;
  std::string last_user_message;
};

// ================================================================
// DefaultContextEngine Tests
// ================================================================

class DefaultContextEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    config_.auto_compact = true;
    config_.compact_max_messages = 10;
    config_.compact_keep_recent = 4;
    config_.context_window = 128000;
    config_.max_tokens = 4096;
  }

  std::shared_ptr<spdlog::logger> logger_;
  AgentConfig config_;
};

TEST_F(DefaultContextEngineTest, NameReturnsDefault) {
  DefaultContextEngine engine(config_, logger_);
  EXPECT_EQ(engine.Name(), "default");
}

TEST_F(DefaultContextEngineTest, AssembleBasic) {
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> history;
  history.push_back(Message{"user", "hello"});
  history.push_back(Message{"assistant", "hi there"});

  auto result = engine.Assemble(history, "You are helpful.", "what is 2+2?",
                                128000, 4096);

  // Should have: system prompt + 2 history + user message = 4
  ASSERT_GE(result.messages.size(), 4u);
  EXPECT_EQ(result.messages[0].role, "system");
  EXPECT_EQ(result.messages[0].text(), "You are helpful.");
  EXPECT_EQ(result.messages.back().role, "user");
  EXPECT_EQ(result.messages.back().text(), "what is 2+2?");
  EXPECT_GT(result.estimated_tokens, 0);
}

TEST_F(DefaultContextEngineTest, AssembleNoSystemPrompt) {
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> history;
  history.push_back(Message{"user", "hello"});

  auto result = engine.Assemble(history, "", "test", 128000, 4096);

  // No system prompt → first message should be from history
  EXPECT_EQ(result.messages[0].role, "user");
  EXPECT_EQ(result.messages[0].text(), "hello");
  EXPECT_EQ(result.messages.back().text(), "test");
}

TEST_F(DefaultContextEngineTest, AssembleAutoCompactsLongHistory) {
  config_.compact_max_messages = 5;
  config_.compact_keep_recent = 3;
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> history;
  for (int i = 0; i < 10; i++) {
    history.push_back(
        Message{i % 2 == 0 ? "user" : "assistant", "msg " + std::to_string(i)});
  }

  auto result =
      engine.Assemble(history, "system", "new question", 128000, 4096);

  // Auto-compaction should have reduced the history
  // Result: system + compaction notice + keep_recent(3) + user message = 6
  // (fewer than 10 + system + user = 12)
  EXPECT_LT(result.messages.size(), 12u);
}

TEST_F(DefaultContextEngineTest, CompactOverflowKeepsRecentHalf) {
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> messages;
  messages.push_back(Message{"system", "sys"});
  for (int i = 0; i < 10; i++) {
    messages.push_back(Message{"user", "msg " + std::to_string(i)});
  }

  auto compacted = engine.CompactOverflow(messages, "sys prompt", 0);

  // keep_recent=0 → defaults to messages.size()/2
  // Should have: system + overflow notice + ~half of messages
  EXPECT_LT(compacted.size(), messages.size());
  EXPECT_EQ(compacted[0].role, "system");
  EXPECT_EQ(compacted[0].text(), "sys prompt");
  EXPECT_EQ(compacted[1].role, "system");
  EXPECT_NE(compacted[1].text().find("overflow"), std::string::npos);
  // Last message preserved
  EXPECT_EQ(compacted.back().text(), "msg 9");
}

TEST_F(DefaultContextEngineTest, CompactOverflowWithExplicitKeepRecent) {
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> messages;
  for (int i = 0; i < 10; i++) {
    messages.push_back(Message{"user", "msg " + std::to_string(i)});
  }

  auto compacted = engine.CompactOverflow(messages, "sys", 3);

  // system + overflow notice + 3 recent = 5
  EXPECT_EQ(compacted.size(), 5u);
  EXPECT_EQ(compacted[2].text(), "msg 7");
  EXPECT_EQ(compacted[3].text(), "msg 8");
  EXPECT_EQ(compacted[4].text(), "msg 9");
}

TEST_F(DefaultContextEngineTest, CompactOverflowNoSystemPrompt) {
  DefaultContextEngine engine(config_, logger_);

  std::vector<Message> messages;
  for (int i = 0; i < 6; i++) {
    messages.push_back(Message{"user", "msg " + std::to_string(i)});
  }

  auto compacted = engine.CompactOverflow(messages, "", 0);

  // No system prompt → starts with overflow notice
  EXPECT_EQ(compacted[0].role, "system");
  EXPECT_NE(compacted[0].text().find("overflow"), std::string::npos);
}

// ================================================================
// Custom Engine Injection Tests
// ================================================================

TEST(ContextEngineInjectionTest, CustomEngineCanBeUsed) {
  auto engine = std::make_shared<TestContextEngine>();

  std::vector<Message> history;
  history.push_back(Message{"user", "hello"});

  auto result = engine->Assemble(history, "sys", "test", 128000, 4096);

  EXPECT_TRUE(engine->assemble_called);
  EXPECT_EQ(engine->last_history_size, 1);
  EXPECT_EQ(engine->last_system_prompt, "sys");
  EXPECT_EQ(engine->last_user_message, "test");
  EXPECT_EQ(result.estimated_tokens, 100);
}

TEST(ContextEngineInjectionTest, CustomCompactOverflow) {
  auto engine = std::make_shared<TestContextEngine>();

  std::vector<Message> messages;
  for (int i = 0; i < 5; i++) {
    messages.push_back(Message{"user", "msg " + std::to_string(i)});
  }

  auto compacted = engine->CompactOverflow(messages, "", 0);

  EXPECT_TRUE(engine->compact_called);
  EXPECT_EQ(compacted.size(), 3u);  // system + last 2
}

TEST(ContextEngineInjectionTest, LifecycleMethodsAreCallable) {
  TestContextEngine engine;

  // These should not throw
  engine.Bootstrap("session-1");
  engine.AfterTurn({}, "session-1");
  engine.OnSubagentSpawn("parent", "child");
  engine.OnSubagentEnded("child");
}

TEST(ContextEngineInjectionTest, NameIsCorrect) {
  TestContextEngine engine;
  EXPECT_EQ(engine.Name(), "test-engine");
}

}  // namespace quantclaw
