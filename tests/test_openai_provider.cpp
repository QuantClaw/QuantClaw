// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <memory>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/providers/openai_provider.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw::detail {
std::string json_nullable_string_or_empty(const nlohmann::json& obj,
                                          std::string_view key);
}

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

TEST(OpenAIProviderCompatibilityTest,
     ChatCompletionRepairsCompatibleToolCallNameAndArguments) {
  const int port = quantclaw::test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  std::atomic<bool> saw_request = false;
  server.Post("/chat/completions",
              [&](const httplib::Request& req, httplib::Response& res) {
                saw_request = true;
                const auto body = nlohmann::json::parse(req.body);
                EXPECT_EQ(body.value("model", ""), "qwen3-max");
                ASSERT_TRUE(body.contains("tools"));

                nlohmann::json tool_call = {
                    {"id", "function.read:1"},
                    {"type", "function"},
                    {"function",
                     {{"name", " functions.read "},
                      {"arguments", "prefix {\"path\":\"/tmp/a.txt\"}x"}}},
                };
                nlohmann::json choice = {
                    {"message",
                     {{"content", nullptr},
                      {"tool_calls", nlohmann::json::array({tool_call})}}},
                    {"finish_reason", "tool_calls"},
                };
                nlohmann::json response = {
                    {"choices", nlohmann::json::array({choice})},
                };
                res.set_content(response.dump(), "application/json");
              });

  std::thread server_thread([&]() {
    quantclaw::test::ReleaseHeldPort(port);
    server.listen("127.0.0.1", port);
  });
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000));

  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  auto logger =
      std::make_shared<spdlog::logger>("openai-compatible-sync", null_sink);
  quantclaw::OpenAIProvider provider(
      "test-key", "http://127.0.0.1:" + std::to_string(port), 30, logger);

  quantclaw::ChatCompletionRequest request;
  request.model = "qwen3-max";
  request.messages.push_back({"user", "Read a file"});
  request.tools.push_back(
      {{"type", "function"},
       {"function",
        {{"name", "read"},
         {"description", "Read file"},
         {"parameters",
          {{"type", "object"},
           {"properties", {{{"path", {{"type", "string"}}}}}}}}}}});

  quantclaw::ChatCompletionResponse response;
  try {
    response = provider.ChatCompletion(request);
  } catch (const std::exception& e) {
    ADD_FAILURE() << "ChatCompletion threw: " << e.what();
  }

  server.stop();
  server_thread.join();

  ASSERT_TRUE(saw_request.load());
  EXPECT_EQ(response.finish_reason, "tool_calls");
  ASSERT_EQ(response.tool_calls.size(), 1u);
  EXPECT_EQ(response.tool_calls[0].id, "function.read:1");
  EXPECT_EQ(response.tool_calls[0].name, "read");
  EXPECT_EQ(response.tool_calls[0].arguments["path"], "/tmp/a.txt");
}

TEST(OpenAIProviderCompatibilityTest,
     StreamingRepairsSplitToolCallAcrossChunks) {
  const int port = quantclaw::test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  std::atomic<bool> saw_request = false;
  server.Post("/chat/completions", [&](const httplib::Request& req,
                                       httplib::Response& res) {
    saw_request = true;
    const auto body = nlohmann::json::parse(req.body);
    EXPECT_EQ(body.value("model", ""), "qwen3-max");
    EXPECT_TRUE(body.value("stream", false));
    ASSERT_TRUE(body.contains("tools"));

    res.set_chunked_content_provider(
        "text/event-stream", [](size_t /*offset*/, httplib::DataSink& sink) {
          const char part1[] =
              "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
              "\"id\":\"function.read:1\",\"function\":{\"name\":\" "
              "functions.read \",\"arguments\":\"prefix {\\\"path\\\":\""
              "}}]}}]}\n\n";
          const char part2[] =
              "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
              "\"function\":{\"arguments\":\"\\\"/tmp/a.txt\\\"}x\"}}]},"
              "\"finish_reason\":\"tool_calls\"}]}\n\n";
          const char part3[] = "data: [DONE]\n\n";
          sink.write(part1, sizeof(part1) - 1);
          sink.write(part2, sizeof(part2) - 1);
          sink.write(part3, sizeof(part3) - 1);
          sink.done();
          return true;
        });
  });

  std::thread server_thread([&]() {
    quantclaw::test::ReleaseHeldPort(port);
    server.listen("127.0.0.1", port);
  });
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000));

  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  auto logger =
      std::make_shared<spdlog::logger>("openai-compatible-stream", null_sink);
  quantclaw::OpenAIProvider provider(
      "test-key", "http://127.0.0.1:" + std::to_string(port), 30, logger);

  quantclaw::ChatCompletionRequest request;
  request.model = "qwen3-max";
  request.stream = true;
  request.messages.push_back({"user", "Read a file"});
  request.tools.push_back(
      {{"type", "function"},
       {"function",
        {{"name", "read"},
         {"description", "Read file"},
         {"parameters",
          {{"type", "object"},
           {"properties", {{{"path", {{"type", "string"}}}}}}}}}}});

  std::vector<quantclaw::ChatCompletionResponse> chunks;
  try {
    provider.ChatCompletionStream(
        request, [&](const quantclaw::ChatCompletionResponse& chunk) {
          chunks.push_back(chunk);
        });
  } catch (const std::exception& e) {
    ADD_FAILURE() << "ChatCompletionStream threw: " << e.what();
  }

  server.stop();
  server_thread.join();

  ASSERT_TRUE(saw_request.load());
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].finish_reason, "tool_calls");
  ASSERT_EQ(chunks[0].tool_calls.size(), 1u);
  EXPECT_EQ(chunks[0].tool_calls[0].id, "function.read:1");
  EXPECT_EQ(chunks[0].tool_calls[0].name, "read");
  EXPECT_EQ(chunks[0].tool_calls[0].arguments["path"], "/tmp/a.txt");
  EXPECT_TRUE(chunks[1].is_stream_end);
}

TEST(OpenAIProviderJsonTest, NullableStringReturnsEmptyForNull) {
  nlohmann::json j = {{"finish_reason", nullptr}};
  EXPECT_EQ(
      quantclaw::detail::json_nullable_string_or_empty(j, "finish_reason"), "");
}

TEST(OpenAIProviderJsonTest, NullableStringReturnsValueForString) {
  nlohmann::json j = {{"finish_reason", "stop"}};
  EXPECT_EQ(
      quantclaw::detail::json_nullable_string_or_empty(j, "finish_reason"),
      "stop");
}

TEST(OpenAIProviderJsonTest, NullableStringReturnsEmptyForMissingField) {
  nlohmann::json j = nlohmann::json::object();
  EXPECT_EQ(
      quantclaw::detail::json_nullable_string_or_empty(j, "finish_reason"), "");
}
