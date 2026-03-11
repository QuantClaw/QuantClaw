// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/providers/openai_provider.hpp"

#include <gtest/gtest.h>

// Mock OpenAIProvider for testing without actual API calls
class MockOpenAIProvider : public quantclaw::OpenAIProvider {
 public:
  MockOpenAIProvider(std::shared_ptr<spdlog::logger> logger)
      : OpenAIProvider("test-key", "https://api.openai.com/v1", 30, logger) {}

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

class OpenAIProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    provider_ = std::make_unique<MockOpenAIProvider>(logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<MockOpenAIProvider> provider_;
};

// --- Basic tests ---

TEST_F(OpenAIProviderTest, ChatCompletion) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "Hello, QuantClaw!"});
  request.model = "gpt-4-turbo";
  request.temperature = 0.7;
  request.max_tokens = 100;

  auto response = provider_->ChatCompletion(request);

  EXPECT_EQ(response.content, "Mock response for: Hello, QuantClaw!");
  EXPECT_EQ(response.finish_reason, "stop");
}

TEST_F(OpenAIProviderTest, ProviderName) {
  EXPECT_EQ(provider_->GetProviderName(), "openai");
}

TEST_F(OpenAIProviderTest, SupportedModels) {
  auto models = provider_->GetSupportedModels();
  EXPECT_FALSE(models.empty());
  // Should include common models
  bool has_gpt4 = false;
  for (const auto& m : models) {
    if (m.find("gpt-4") != std::string::npos)
      has_gpt4 = true;
  }
  EXPECT_TRUE(has_gpt4);
}

TEST_F(OpenAIProviderTest, StreamingCompletion) {
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

// --- Request/Response struct tests ---

TEST_F(OpenAIProviderTest, RequestDefaults) {
  quantclaw::ChatCompletionRequest req;
  EXPECT_DOUBLE_EQ(req.temperature, 0.7);
  EXPECT_EQ(req.max_tokens, 8192);
  EXPECT_TRUE(req.tool_choice_auto);
  EXPECT_FALSE(req.stream);
  EXPECT_TRUE(req.tools.empty());
  EXPECT_TRUE(req.messages.empty());
}

TEST_F(OpenAIProviderTest, ResponseDefaults) {
  quantclaw::ChatCompletionResponse resp;
  EXPECT_TRUE(resp.content.empty());
  EXPECT_TRUE(resp.tool_calls.empty());
  EXPECT_TRUE(resp.finish_reason.empty());
  EXPECT_FALSE(resp.is_stream_end);
}

TEST_F(OpenAIProviderTest, ToolCallStruct) {
  quantclaw::ToolCall tc;
  tc.id = "call_123";
  tc.name = "read";
  tc.arguments = {{"path", "/tmp/test.txt"}};

  EXPECT_EQ(tc.id, "call_123");
  EXPECT_EQ(tc.name, "read");
  EXPECT_EQ(tc.arguments["path"], "/tmp/test.txt");
}

// --- Mock captures request ---

TEST_F(OpenAIProviderTest, ChatCompletionPassesModel) {
  quantclaw::ChatCompletionRequest request;
  request.messages.push_back({"user", "test"});
  request.model = "custom-model";
  request.temperature = 0.5;
  request.max_tokens = 200;

  provider_->ChatCompletion(request);

  EXPECT_EQ(provider_->last_request.model, "custom-model");
  EXPECT_DOUBLE_EQ(provider_->last_request.temperature, 0.5);
  EXPECT_EQ(provider_->last_request.max_tokens, 200);
}

TEST_F(OpenAIProviderTest, ChatCompletionMultipleMessages) {
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

TEST_F(OpenAIProviderTest, ResponseWithToolCalls) {
  provider_->next_response.finish_reason = "tool_calls";
  quantclaw::ToolCall tc;
  tc.id = "call_abc";
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

TEST_F(OpenAIProviderTest, StreamingMultipleChunks) {
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

// --- Construction with empty base URL defaults ---

TEST_F(OpenAIProviderTest, ConstructionWithEmptyBaseUrl) {
  // Empty base_url should default to OpenAI
  EXPECT_NO_THROW(
      { quantclaw::OpenAIProvider provider("key", "", 10, logger_); });
}

TEST_F(OpenAIProviderTest, ConstructionWithCustomBaseUrl) {
  EXPECT_NO_THROW({
    quantclaw::OpenAIProvider provider("key", "https://custom.api.com/v1", 30,
                                       logger_);
  });
}
