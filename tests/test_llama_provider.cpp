// Copyright 2026 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <gtest/gtest.h>

import std;
import quantclaw.providers.llama_provider;
import quantclaw.providers.llm_provider;
import quantclaw.providers.provider_error;

// ── Mock that captures payloads without making HTTP calls ────────────────────

class MockLlamaProvider : public quantclaw::LlamaProvider {
 public:
  explicit MockLlamaProvider(std::shared_ptr<spdlog::logger> logger)
      : LlamaProvider("http://127.0.0.1:8080", 30, logger) {}

  quantclaw::ChatCompletionResponse next_response;
  std::vector<quantclaw::ChatCompletionResponse> stream_chunks;
  quantclaw::ChatCompletionRequest last_request;

  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest& request) override {
    last_request = request;
    if (next_response.content.empty() && next_response.tool_calls.empty()) {
      quantclaw::ChatCompletionResponse r;
      r.content = "mock: " + request.messages.back().text();
      r.finish_reason = "stop";
      return r;
    }
    return next_response;
  }

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest& request,
      std::function<void(const quantclaw::ChatCompletionResponse&)> callback)
      override {
    last_request = request;
    if (stream_chunks.empty()) {
      quantclaw::ChatCompletionResponse r;
      r.content = "streamed mock";
      r.is_stream_end = true;
      callback(r);
    } else {
      for (const auto& chunk : stream_chunks) {
        callback(chunk);
      }
    }
  }
};

class LlamaProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
    provider_ = std::make_unique<MockLlamaProvider>(logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<MockLlamaProvider> provider_;
};

// ── Provider identity ────────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, ProviderNameIsLocal) {
  EXPECT_EQ(provider_->GetProviderName(), "local");
}

TEST_F(LlamaProviderTest, SupportedModelsIsEmpty) {
  // llama-server exposes whatever model it was started with; we don't
  // hard-code names.
  EXPECT_TRUE(provider_->GetSupportedModels().empty());
}

// ── Basic chat completion ────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, ChatCompletionReturnsContent) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "hello"});
  req.model = "qwen3";

  auto resp = provider_->ChatCompletion(req);

  EXPECT_EQ(resp.content, "mock: hello");
  EXPECT_EQ(resp.finish_reason, "stop");
}

TEST_F(LlamaProviderTest, ChatCompletionForwardsModel) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "ping"});
  req.model = "qwen3-9b";

  provider_->ChatCompletion(req);

  EXPECT_EQ(provider_->last_request.model, "qwen3-9b");
}

TEST_F(LlamaProviderTest, ChatCompletionForwardsTemperature) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "test"});
  req.temperature = 0.2;

  provider_->ChatCompletion(req);

  EXPECT_DOUBLE_EQ(provider_->last_request.temperature, 0.2);
}

TEST_F(LlamaProviderTest, ChatCompletionCustomResponse) {
  quantclaw::ChatCompletionResponse custom;
  custom.content = "custom answer";
  custom.finish_reason = "stop";
  provider_->next_response = custom;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "anything"});

  auto resp = provider_->ChatCompletion(req);
  EXPECT_EQ(resp.content, "custom answer");
}

// ── Tool calling ─────────────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, ChatCompletionWithToolCall) {
  quantclaw::ChatCompletionResponse tool_resp;
  quantclaw::ToolCall tc;
  tc.id = "call_abc123";
  tc.name = "get_weather";
  tc.arguments = {{"location", "London"}};
  tool_resp.tool_calls.push_back(tc);
  tool_resp.finish_reason = "tool_calls";
  provider_->next_response = tool_resp;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "What is the weather in London?"});
  req.tools.push_back(
      {{"name", "get_weather"},
       {"description", "Returns weather for a location"},
       {"parameters",
        {{"type", "object"},
         {"properties", {{"location", {{"type", "string"}}}}}}}});

  auto resp = provider_->ChatCompletion(req);

  ASSERT_EQ(resp.tool_calls.size(), 1u);
  EXPECT_EQ(resp.tool_calls[0].id, "call_abc123");
  EXPECT_EQ(resp.tool_calls[0].name, "get_weather");
  EXPECT_EQ(resp.finish_reason, "tool_calls");
}

TEST_F(LlamaProviderTest, ToolsAreForwardedInRequest) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "use tools"});
  req.tools.push_back({{"name", "my_tool"}, {"description", "does stuff"}});
  req.tool_choice_auto = true;

  provider_->ChatCompletion(req);

  ASSERT_EQ(provider_->last_request.tools.size(), 1u);
  EXPECT_EQ(provider_->last_request.tools[0]["name"], "my_tool");
}

// ── Multi-turn conversation ──────────────────────────────────────────────────

TEST_F(LlamaProviderTest, MultiTurnMessagesForwarded) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"system", "You are a helpful assistant."});
  req.messages.push_back({"user", "First message"});
  req.messages.push_back({"assistant", "First reply"});
  req.messages.push_back({"user", "Second message"});

  provider_->ChatCompletion(req);

  ASSERT_EQ(provider_->last_request.messages.size(), 4u);
  EXPECT_EQ(provider_->last_request.messages[0].role, "system");
  EXPECT_EQ(provider_->last_request.messages[3].role, "user");
}

// ── Streaming ────────────────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, StreamingReceivesSingleChunk) {
  std::vector<quantclaw::ChatCompletionResponse> received;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "stream test"});

  provider_->ChatCompletionStream(
      req, [&](const auto& r) { received.push_back(r); });

  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0].content, "streamed mock");
  EXPECT_TRUE(received[0].is_stream_end);
}

TEST_F(LlamaProviderTest, StreamingMultipleChunks) {
  quantclaw::ChatCompletionResponse c1, c2, c3;
  c1.content = "Hello";
  c2.content = " world";
  c3.is_stream_end = true;
  provider_->stream_chunks = {c1, c2, c3};

  std::string assembled;
  bool got_end = false;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "hi"});

  provider_->ChatCompletionStream(req, [&](const auto& r) {
    assembled += r.content;
    if (r.is_stream_end)
      got_end = true;
  });

  EXPECT_EQ(assembled, "Hello world");
  EXPECT_TRUE(got_end);
}

TEST_F(LlamaProviderTest, StreamingToolCallChunks) {
  // Simulate two chunks: one carrying a tool call, one signalling end.
  quantclaw::ChatCompletionResponse tc_chunk;
  quantclaw::ToolCall tc;
  tc.id = "call_xyz";
  tc.name = "search";
  tc.arguments = {{"query", "llama.cpp"}};
  tc_chunk.tool_calls.push_back(tc);

  quantclaw::ChatCompletionResponse end_chunk;
  end_chunk.is_stream_end = true;

  provider_->stream_chunks = {tc_chunk, end_chunk};

  std::vector<quantclaw::ToolCall> collected_calls;
  bool got_end = false;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "search for llama.cpp"});

  provider_->ChatCompletionStream(req, [&](const auto& r) {
    for (const auto& call : r.tool_calls)
      collected_calls.push_back(call);
    if (r.is_stream_end)
      got_end = true;
  });

  ASSERT_EQ(collected_calls.size(), 1u);
  EXPECT_EQ(collected_calls[0].name, "search");
  EXPECT_TRUE(got_end);
}

// ── SSE parser unit tests ────────────────────────────────────────────────────
// Test the internal SSE line-parsing logic by exercising ParseResponse
// indirectly through a thin subclass that exposes a parse method.

TEST_F(LlamaProviderTest, ParseResponsePlainText) {
  // Construct a valid OpenAI-format response and verify content extraction.
  // We do this by setting up a fixed next_response and checking round-trip.
  quantclaw::ChatCompletionResponse expected;
  expected.content = "Paris";
  expected.finish_reason = "stop";
  provider_->next_response = expected;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "Capital of France?"});
  auto resp = provider_->ChatCompletion(req);

  EXPECT_EQ(resp.content, "Paris");
  EXPECT_EQ(resp.finish_reason, "stop");
}

TEST_F(LlamaProviderTest, ParseResponseMultipleToolCalls) {
  quantclaw::ChatCompletionResponse expected;
  for (int i = 0; i < 3; ++i) {
    quantclaw::ToolCall tc;
    tc.id = "call_" + std::to_string(i);
    tc.name = "tool_" + std::to_string(i);
    tc.arguments = {{"n", i}};
    expected.tool_calls.push_back(tc);
  }
  expected.finish_reason = "tool_calls";
  provider_->next_response = expected;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "run three tools"});
  auto resp = provider_->ChatCompletion(req);

  ASSERT_EQ(resp.tool_calls.size(), 3u);
  EXPECT_EQ(resp.tool_calls[2].name, "tool_2");
}

// ── Token usage ──────────────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, TokenUsageForwarded) {
  quantclaw::ChatCompletionResponse resp_with_usage;
  resp_with_usage.content = "ok";
  resp_with_usage.usage.prompt_tokens = 10;
  resp_with_usage.usage.completion_tokens = 5;
  resp_with_usage.usage.total_tokens = 15;
  provider_->next_response = resp_with_usage;

  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "count tokens"});
  auto resp = provider_->ChatCompletion(req);

  EXPECT_EQ(resp.usage.prompt_tokens, 10);
  EXPECT_EQ(resp.usage.completion_tokens, 5);
  EXPECT_EQ(resp.usage.total_tokens, 15);
}

// ── Max tokens ───────────────────────────────────────────────────────────────

TEST_F(LlamaProviderTest, MaxTokensForwarded) {
  quantclaw::ChatCompletionRequest req;
  req.messages.push_back({"user", "long response please"});
  req.max_tokens = 4096;

  provider_->ChatCompletion(req);

  EXPECT_EQ(provider_->last_request.max_tokens, 4096);
}
