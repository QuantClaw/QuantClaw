// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/usage_accumulator.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Mock LLM provider that returns canned responses and captures requests
class MockLLMProvider : public quantclaw::LLMProvider {
 public:
  std::string response_text = "I am QuantClaw.";
  std::string response_finish_reason = "stop";
  std::vector<quantclaw::ToolCall> response_tool_calls;
  std::vector<quantclaw::ChatCompletionResponse> stream_chunks;
  mutable quantclaw::ChatCompletionRequest last_request;

  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest& request) override {
    last_request = request;
    quantclaw::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.finish_reason = response_finish_reason;
    resp.tool_calls = response_tool_calls;
    resp.usage = mock_usage;
    return resp;
  }

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest& request,
      std::function<void(const quantclaw::ChatCompletionResponse&)> callback)
      override {
    last_request = request;
    if (!stream_chunks.empty()) {
      for (const auto& chunk : stream_chunks) {
        callback(chunk);
      }
      return;
    }
    quantclaw::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.is_stream_end = true;
    resp.usage = mock_usage;
    callback(resp);
  }

  std::string GetProviderName() const override {
    return "mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }

  // Configurable token counts for usage-tracking tests (default 0 keeps
  // existing tests unaffected)
  quantclaw::TokenUsage mock_usage;
};

class AgentLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_agent_test");

    auto log_sink =
        std::make_shared<spdlog::sinks::ostream_sink_mt>(log_stream_);
    logger_ = std::make_shared<spdlog::logger>("test", log_sink);
    logger_->set_level(spdlog::level::debug);
    logger_->flush_on(spdlog::level::debug);
    logger_->set_pattern("%l:%v");

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(test_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_provider_ = std::make_shared<MockLLMProvider>();

    quantclaw::AgentConfig agent_config;
    agent_config.model = "test-model";
    agent_config.temperature = 0.5;
    agent_config.max_tokens = 2048;
    agent_config.max_iterations = 15;

    agent_loop_ = std::make_unique<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_provider_,
        agent_config, logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::ostringstream log_stream_;
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<MockLLMProvider> mock_provider_;
  std::unique_ptr<quantclaw::AgentLoop> agent_loop_;
};

TEST_F(AgentLoopTest, ProcessMessageReturnsResponse) {
  mock_provider_->response_text = "Hello! I am QuantClaw.";

  auto new_msgs =
      agent_loop_->ProcessMessage("Hello", {}, "You are a helpful assistant.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "Hello! I am QuantClaw.");
}

TEST_F(AgentLoopTest, ProcessMessageWithHistory) {
  quantclaw::Message prev_user{"user", "What is your name?"};
  quantclaw::Message prev_assistant{"assistant", "I am QuantClaw."};

  std::vector<quantclaw::Message> history = {prev_user, prev_assistant};

  mock_provider_->response_text = "You asked about my name.";

  auto new_msgs = agent_loop_->ProcessMessage("What did I just ask?", history,
                                              "You are helpful.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "You asked about my name.");
}

TEST_F(AgentLoopTest, ProcessMessageWithEmptySystemPrompt) {
  mock_provider_->response_text = "Response without system prompt.";

  auto new_msgs = agent_loop_->ProcessMessage("Test", {}, "");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "Response without system prompt.");
}

TEST_F(AgentLoopTest, StreamingCallback) {
  mock_provider_->response_text = "Streamed response.";

  std::vector<quantclaw::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const quantclaw::AgentEvent& event) {
        events.push_back(event);
      });

  // Should have at least a text_delta and message_end
  EXPECT_FALSE(events.empty());
}

TEST_F(AgentLoopTest, StopInterruptsProcessing) {
  agent_loop_->Stop();

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "System.");

  // Should return stop message or the mock response (depending on timing)
  EXPECT_FALSE(new_msgs.empty());
}

TEST_F(AgentLoopTest, SetMaxIterations) {
  agent_loop_->SetMaxIterations(3);
  // Should not throw
  mock_provider_->response_text = "ok";
  auto new_msgs = agent_loop_->ProcessMessage("test", {}, "sys");
  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().content[0].text, "ok");
}

// --- AgentConfig injection tests ---

TEST_F(AgentLoopTest, UsesConfigModel) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.model, "test-model");
}

TEST_F(AgentLoopTest, UsesConfigTemperature) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_DOUBLE_EQ(mock_provider_->last_request.temperature, 0.5);
}

TEST_F(AgentLoopTest, UsesConfigMaxTokens) {
  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.max_tokens, 2048);
}

TEST_F(AgentLoopTest, SetConfigUpdatesModel) {
  quantclaw::AgentConfig new_config;
  new_config.model = "new-model";
  new_config.temperature = 0.9;
  new_config.max_tokens = 8192;
  new_config.max_iterations = 5;

  agent_loop_->SetConfig(new_config);

  mock_provider_->response_text = "ok";
  agent_loop_->ProcessMessage("test", {}, "sys");

  EXPECT_EQ(mock_provider_->last_request.model, "new-model");
  EXPECT_DOUBLE_EQ(mock_provider_->last_request.temperature, 0.9);
  EXPECT_EQ(mock_provider_->last_request.max_tokens, 8192);
}

TEST_F(AgentLoopTest, StreamingUsesConfigModel) {
  mock_provider_->response_text = "streamed";
  std::vector<quantclaw::AgentEvent> events;
  agent_loop_->ProcessMessageStream(
      "test", {}, "sys", [&events](const quantclaw::AgentEvent& event) {
        events.push_back(event);
      });

  EXPECT_EQ(mock_provider_->last_request.model, "test-model");
  EXPECT_TRUE(mock_provider_->last_request.stream);
}

TEST_F(AgentLoopTest, GetConfigReturnsCurrentConfig) {
  const auto& config = agent_loop_->GetConfig();
  EXPECT_EQ(config.model, "test-model");
  EXPECT_DOUBLE_EQ(config.temperature, 0.5);
  EXPECT_EQ(config.max_tokens, 2048);
}

// --- Streaming returns new messages ---

TEST_F(AgentLoopTest, StreamReturnsNewMessages) {
  mock_provider_->response_text = "Final answer.";

  std::vector<quantclaw::AgentEvent> events;
  auto new_msgs = agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const quantclaw::AgentEvent& event) {
        events.push_back(event);
      });

  // Should have at least 1 assistant message (the final response)
  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_FALSE(new_msgs.back().content.empty());
  EXPECT_EQ(new_msgs.back().content[0].type, "text");
  EXPECT_EQ(new_msgs.back().content[0].text, "Final answer.");
}

TEST_F(AgentLoopTest, StreamInvalidToolCallReturnsReadableFallback) {
  quantclaw::ChatCompletionResponse tool_chunk;
  tool_chunk.tool_calls.push_back({"call_invalid", "", {{"command", "ver"}}});

  quantclaw::ChatCompletionResponse end_chunk;
  end_chunk.is_stream_end = true;

  mock_provider_->stream_chunks = {tool_chunk, end_chunk};

  std::vector<quantclaw::AgentEvent> events;
  auto new_msgs = agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const quantclaw::AgentEvent& event) {
        events.push_back(event);
      });

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  ASSERT_FALSE(new_msgs.back().content.empty());
  EXPECT_EQ(new_msgs.back().content[0].text,
            "I couldn't continue because the model emitted an invalid tool "
            "call. Please try again.");
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().type, quantclaw::gateway::events::kMessageEnd);
  EXPECT_EQ(events.back().data.value("content", ""),
            "I couldn't continue because the model emitted an invalid tool "
            "call. Please try again.");
}

TEST_F(AgentLoopTest, StreamEmptyResponseReturnsReadableFallbackAndLogsWhy) {
  quantclaw::ChatCompletionResponse end_chunk;
  end_chunk.is_stream_end = true;

  mock_provider_->stream_chunks = {end_chunk};

  std::vector<quantclaw::AgentEvent> events;
  auto new_msgs = agent_loop_->ProcessMessageStream(
      "Hello", {}, "System.", [&events](const quantclaw::AgentEvent& event) {
        events.push_back(event);
      });

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  ASSERT_FALSE(new_msgs.back().content.empty());
  EXPECT_EQ(new_msgs.back().content[0].text,
            "I couldn't complete that request because the model returned no "
            "usable response. Please try again.");
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().type, quantclaw::gateway::events::kMessageEnd);
  EXPECT_EQ(events.back().data.value("content", ""),
            "I couldn't complete that request because the model returned no "
            "usable response. Please try again.");
  EXPECT_NE(log_stream_.str().find("Empty streaming response details:"),
            std::string::npos);
}

TEST_F(AgentLoopTest, NonStreamInvalidToolCallReturnsReadableFallback) {
  mock_provider_->response_text.clear();
  mock_provider_->response_tool_calls = {
      {"call_invalid", "", {{"command", "ver"}}}};

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  ASSERT_FALSE(new_msgs.back().content.empty());
  EXPECT_EQ(new_msgs.back().content[0].text,
            "I couldn't continue because the model emitted an invalid tool "
            "call. Please try again.");
}

TEST_F(AgentLoopTest, NonStreamReturnsNewMessages) {
  mock_provider_->response_text = "Non-stream final.";

  auto new_msgs = agent_loop_->ProcessMessage("Hello", {}, "System.");

  ASSERT_FALSE(new_msgs.empty());
  EXPECT_EQ(new_msgs.back().role, "assistant");
  EXPECT_EQ(new_msgs.back().content[0].text, "Non-stream final.");
}

// --- usage_session_key routing tests ---

// Helper fixture alias for clarity
class AgentLoopUsageKeyTest : public AgentLoopTest {
 protected:
  void SetUp() override {
    AgentLoopTest::SetUp();
    accumulator_ = std::make_shared<quantclaw::UsageAccumulator>();
    agent_loop_->SetUsageAccumulator(accumulator_);
    agent_loop_->SetSessionKey("internal-session");
    // Provide non-zero tokens so turns > 0 regardless of actual content
    mock_provider_->mock_usage = {10, 5, 15};
    mock_provider_->response_text = "ok";
  }

  std::shared_ptr<quantclaw::UsageAccumulator> accumulator_;
};

// When usage_session_key is non-empty, usage is recorded under that key (not
// session_key_)
TEST_F(AgentLoopUsageKeyTest, UsageRecordedUnderCustomKey) {
  agent_loop_->ProcessMessage("hi", {}, "sys", "custom-key");

  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 0);
}

// When usage_session_key is empty, usage falls back to session_key_
TEST_F(AgentLoopUsageKeyTest, UsageFallsBackToSessionKey) {
  agent_loop_->ProcessMessage("hi", {},
                              "sys");  // usage_session_key defaults to ""

  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 0);
}

// When both usage_session_key and session_key_ are empty, no usage is recorded
TEST_F(AgentLoopUsageKeyTest, NeitherKeySetNoUsageRecorded) {
  agent_loop_->SetSessionKey("");
  agent_loop_->ProcessMessage("hi", {}, "sys");

  EXPECT_EQ(accumulator_->GetGlobal().turns, 0);
}

// Stream: non-empty usage_session_key routes usage to that key
TEST_F(AgentLoopUsageKeyTest, StreamUsageRecordedUnderCustomKey) {
  agent_loop_->ProcessMessageStream(
      "hi", {}, "sys", [](const quantclaw::AgentEvent&) {}, "custom-key");

  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 0);
}

// Stream: empty usage_session_key falls back to session_key_
TEST_F(AgentLoopUsageKeyTest, StreamUsageFallsBackToSessionKey) {
  agent_loop_->ProcessMessageStream("hi", {}, "sys",
                                    [](const quantclaw::AgentEvent&) {});

  EXPECT_EQ(accumulator_->GetSession("internal-session").turns, 1);
  EXPECT_EQ(accumulator_->GetSession("custom-key").turns, 0);
}
