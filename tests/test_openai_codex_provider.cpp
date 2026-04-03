// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <memory>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/auth/openai_codex_auth.hpp"
#include "quantclaw/providers/openai_codex_provider.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw {
namespace {

std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, sink);
}

class FakeBearerTokenSource : public auth::BearerTokenSource {
 public:
  std::string token = "oauth-token";
  int calls = 0;

  std::string ResolveAccessToken() override {
    ++calls;
    return token;
  }
};

}  // namespace

TEST(OpenAICodexProviderTest, ChatCompletionUsesBearerTokenAndParsesOutput) {
  const int port = test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  std::atomic<bool> saw_request = false;
  server.Post("/codex/responses", [&](const httplib::Request& req,
                                      httplib::Response& res) {
    saw_request = true;
    EXPECT_EQ(req.get_header_value("Authorization"), "Bearer oauth-token");

    auto body = nlohmann::json::parse(req.body);
    EXPECT_EQ(body.value("model", ""), "gpt-5");
    EXPECT_EQ(body.value("instructions", ""), "You are helpful.");
    EXPECT_EQ(body.value("stream", false), true);
    EXPECT_EQ(body.value("store", true), false);
    EXPECT_FALSE(body.contains("temperature"));
    EXPECT_EQ(body.value("max_output_tokens", 0), 8192);
    ASSERT_TRUE(body.contains("input"));
    ASSERT_TRUE(body["input"].is_array());
    ASSERT_FALSE(body["input"].empty());

    res.set_chunked_content_provider(
        "text/event-stream", [](size_t /*offset*/, httplib::DataSink& sink) {
          const char event1[] =
              "data: {\"type\":\"response.output_text.delta\",\"delta\":"
              "\"Hello from Codex\"}\n\n";
          const char event2[] =
              "data: {\"type\":\"response.completed\",\"response\":"
              "{\"status\":\"completed\",\"usage\":{\"input_tokens\":11,"
              "\"output_tokens\":7,\"total_tokens\":18}}}\n\n";
          sink.write(event1, sizeof(event1) - 1);
          sink.write(event2, sizeof(event2) - 1);
          sink.done();
          return true;
        });
  });

  std::thread server_thread([&]() {
    test::ReleaseHeldPort(port);
    server.listen("127.0.0.1", port);
  });
  ASSERT_TRUE(test::WaitForServerReady(port));

  auto logger = make_logger("openai-codex-provider");
  auto token_source = std::make_shared<FakeBearerTokenSource>();
  OpenAICodexProvider provider("http://127.0.0.1:" + std::to_string(port), 30,
                               logger, token_source);

  ChatCompletionRequest request;
  request.model = "gpt-5";
  request.messages.push_back({"system", "You are helpful."});
  request.messages.push_back({"user", "Say hello"});

  const auto response = provider.ChatCompletion(request);
  EXPECT_TRUE(saw_request.load());
  EXPECT_EQ(token_source->calls, 1);
  EXPECT_EQ(response.content, "Hello from Codex");
  EXPECT_EQ(response.usage.prompt_tokens, 11);
  EXPECT_EQ(response.usage.completion_tokens, 7);
  EXPECT_EQ(response.usage.total_tokens, 18);

  server.stop();
  server_thread.join();
}

TEST(OpenAICodexProviderTest, StreamingParsesTextDeltas) {
  const int port = test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  server.Post("/codex/responses", [&](const httplib::Request& req,
                                      httplib::Response& res) {
    EXPECT_EQ(req.get_header_value("Authorization"), "Bearer oauth-token");
    auto body = nlohmann::json::parse(req.body);
    EXPECT_EQ(body.value("model", ""), "gpt-5");
    EXPECT_EQ(body.value("instructions", ""), "");
    EXPECT_EQ(body.value("stream", false), true);
    EXPECT_EQ(body.value("store", true), false);
    EXPECT_FALSE(body.contains("temperature"));
    EXPECT_EQ(body.value("max_output_tokens", 0), 8192);
    res.set_chunked_content_provider(
        "text/event-stream", [](size_t /*offset*/, httplib::DataSink& sink) {
          const char event1[] =
              "data: {\"type\":\"response.output_text.delta\",\"delta\":"
              "\"Hello \"}\n\n";
          const char event2[] =
              "data: {\"type\":\"response.output_text.delta\",\"delta\":"
              "\"world\"}\n\n";
          const char event3[] =
              "data: {\"type\":\"response.completed\",\"response\":"
              "{\"status\":\"completed\"}}\n\n";
          sink.write(event1, sizeof(event1) - 1);
          sink.write(event2, sizeof(event2) - 1);
          sink.write(event3, sizeof(event3) - 1);
          sink.done();
          return true;
        });
  });

  std::thread server_thread([&]() {
    test::ReleaseHeldPort(port);
    server.listen("127.0.0.1", port);
  });
  ASSERT_TRUE(test::WaitForServerReady(port));

  auto logger = make_logger("openai-codex-provider-stream");
  auto token_source = std::make_shared<FakeBearerTokenSource>();
  OpenAICodexProvider provider("http://127.0.0.1:" + std::to_string(port), 30,
                               logger, token_source);

  ChatCompletionRequest request;
  request.model = "gpt-5";
  request.stream = true;
  request.messages.push_back({"user", "Say hello"});

  std::string text;
  bool saw_end = false;
  provider.ChatCompletionStream(request,
                                [&](const ChatCompletionResponse& chunk) {
                                  text += chunk.content;
                                  if (chunk.is_stream_end) {
                                    saw_end = true;
                                  }
                                });

  EXPECT_EQ(text, "Hello world");
  EXPECT_TRUE(saw_end);

  server.stop();
  server_thread.join();
}

}  // namespace quantclaw
