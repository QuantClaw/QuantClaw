// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/anthropic_provider.hpp"
#include "quantclaw/providers/llm_provider.hpp"

#include <gtest/gtest.h>

// Mock AnthropicProvider for testing without actual API calls
class MockAnthropicProvider : public quantclaw::AnthropicProvider {
 public:
  MockAnthropicProvider(std::shared_ptr<spdlog::logger> logger)
      : AnthropicProvider("test-key", "https://api.anthropic.com", 30, logger) {
  }

  // Configurable response
  quantclaw::ChatCompletionResponse next_response;

  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest& request) override {
    last_request = request;
    if (next_response.content.empty() && next_response.tool_calls.empty()) {
      quantclaw::ChatCompletionResponse response;
      response.content = "Mock response for: " + request.messages.back().text();
      response.finish_reason = "stop";
      return response;
    }
    return next_response;
  }

  // Stream emits multiple chunks
  std::vector<quantclaw::ChatCompletionResponse> stream_chunks;

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest& /*request*/,
      std::function<void(const quantclaw::ChatCompletionResponse&)> callback)
      override {
    if (stream_chunks.empty()) {
      quantclaw::ChatCompletionResponse response;
      response.content = "Streamed mock";
      response.is_stream_end = true;
      callback(response);
    } else {
      for (const auto& chunk : stream_chunks) {
        callback(chunk);
      }
    }
  }

  quantclaw::ChatCompletionRequest last_request;
};

class AnthropicProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    provider_ = std::make_unique<MockAnthropicProvider>(logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<MockAnthropicProvider> provider_;
};

// --- Basic tests ---

TEST_F(AnthropicProviderTest, ChatCompletion) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "Hello, QuantClaw!"});
  request.model = "claude-sonnet-4-6";
  request.temperature = 0.7;
  request.max_tokens = 4096;

  auto response = provider_->ChatCompletion(request);

  EXPECT_EQ(response.content, "Mock response for: Hello, QuantClaw!");
  EXPECT_EQ(response.finish_reason, "stop");
}

TEST_F(AnthropicProviderTest, ProviderName) {
  EXPECT_EQ(provider_->GetProviderName(), "anthropic");
}

TEST_F(AnthropicProviderTest, SupportedModels) {
  auto models = provider_->GetSupportedModels();
  EXPECT_FALSE(models.empty());
  bool has_claude = false;
  for (const auto& m : models) {
    if (m.find("claude") != std::string::npos)
      has_claude = true;
  }
  EXPECT_TRUE(has_claude);
}

TEST_F(AnthropicProviderTest, StreamingCompletion) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "Hello"});

  bool called = false;
  provider_->ChatCompletionStream(
      request, [&called](const quantclaw::ChatCompletionResponse& resp) {
        called = true;
        EXPECT_TRUE(resp.is_stream_end);
      });

  EXPECT_TRUE(called);
}

// --- Mock captures request ---

TEST_F(AnthropicProviderTest, ChatCompletionPassesModel) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "test"});
  request.model = "claude-opus-4-6";
  request.temperature = 0.5;
  request.max_tokens = 2048;

  provider_->ChatCompletion(request);

  EXPECT_EQ(provider_->last_request.model, "claude-opus-4-6");
  EXPECT_DOUBLE_EQ(provider_->last_request.temperature, 0.5);
  EXPECT_EQ(provider_->last_request.max_tokens, 2048);
}

TEST_F(AnthropicProviderTest, ChatCompletionMultipleMessages) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"system", "You are helpful."});
  request.messages.push_back({"user", "First message"});
  request.messages.push_back({"assistant", "First reply"});
  request.messages.push_back({"user", "Second message"});

  auto response = provider_->ChatCompletion(request);

  EXPECT_EQ(provider_->last_request.messages.size(), 4u);
  EXPECT_EQ(response.content, "Mock response for: Second message");
}

// --- Tool call response ---

TEST_F(AnthropicProviderTest, ResponseWithToolCalls) {
  provider_->next_response.finish_reason = "tool_calls";
  quantclaw::ToolCall tc;
  tc.id = "toolu_abc";
  tc.name = "exec";
  tc.arguments = {{"command", "ls"}};
  provider_->next_response.tool_calls.push_back(tc);

  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "List files"});

  auto response = provider_->ChatCompletion(request);

  EXPECT_EQ(response.finish_reason, "tool_calls");
  ASSERT_EQ(response.tool_calls.size(), 1u);
  EXPECT_EQ(response.tool_calls[0].name, "exec");
  EXPECT_EQ(response.tool_calls[0].arguments["command"], "ls");
}

// --- Multi-chunk streaming ---

TEST_F(AnthropicProviderTest, StreamingMultipleChunks) {
  provider_->stream_chunks = {
      {/*.content=*/"Hello ", {}, "", false},
      {/*.content=*/"world", {}, "", false},
      {/*.content=*/"", {}, "", true}  // stream end
  };

  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "test"});
  request.stream = true;

  std::string accumulated;
  bool saw_end = false;

  provider_->ChatCompletionStream(
      request, [&](const quantclaw::ChatCompletionResponse& resp) {
        accumulated += resp.content;
        if (resp.is_stream_end)
          saw_end = true;
      });

  EXPECT_EQ(accumulated, "Hello world");
  EXPECT_TRUE(saw_end);
}

// --- Construction ---

TEST_F(AnthropicProviderTest, ConstructionWithEmptyBaseUrl) {
  EXPECT_NO_THROW(
      { quantclaw::AnthropicProvider provider("key", "", 10, logger_); });
}

TEST_F(AnthropicProviderTest, ConstructionWithCustomBaseUrl) {
  EXPECT_NO_THROW({
    quantclaw::AnthropicProvider provider("key", "https://custom.anthropic.com",
                                          30, logger_);
  });
}
