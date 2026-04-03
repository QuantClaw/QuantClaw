// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <string>
#include <thread>

#include <httplib.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/auth/github_copilot_auth.hpp"
#include "quantclaw/providers/github_copilot_provider.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw {
namespace {

std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, sink);
}

class FakeRuntimeResolver : public auth::GitHubCopilotRuntimeResolverInterface {
 public:
  auth::GitHubCopilotRuntimeCredential result;
  int resolve_calls = 0;

  auth::GitHubCopilotRuntimeCredential ResolveRuntimeCredential() override {
    ++resolve_calls;
    return result;
  }
};

}  // namespace

TEST(GitHubCopilotProviderTest, ChatCompletionUsesResolvedTokenAndBaseUrl) {
  const int port = test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  std::string seen_auth;
  std::string seen_user_agent;
  std::string seen_editor_version;
  std::string seen_editor_plugin_version;

  server.Post("/chat/completions", [&](const httplib::Request& req,
                                       httplib::Response& res) {
    seen_auth = req.get_header_value("Authorization");
    seen_user_agent = req.get_header_value("User-Agent");
    seen_editor_version = req.get_header_value("Editor-Version");
    seen_editor_plugin_version = req.get_header_value("Editor-Plugin-Version");
    res.set_content(
        R"({"choices":[{"message":{"content":"copilot ok"},"finish_reason":"stop"}]})",
        "application/json");
  });

  std::thread thread([&]() {
    test::ReleaseHeldPort(port);
    server.listen("127.0.0.1", port);
  });
  const auto stop_server = [&]() {
    server.stop();
    if (thread.joinable()) {
      thread.join();
    }
  };

  if (!test::WaitForServerReady(port)) {
    stop_server();
    FAIL() << "Timed out waiting for mock Copilot server to accept connections";
  }

  auto logger = make_logger("github-copilot-provider");
  auto resolver = std::make_shared<FakeRuntimeResolver>();
  resolver->result.api_token = "copilot-runtime-token";
  resolver->result.base_url = "http://127.0.0.1:" + std::to_string(port);

  GitHubCopilotProvider provider(30, logger, resolver);
  ChatCompletionRequest request;
  request.model = "gpt-4o";
  request.messages.push_back({"user", "hello"});

  ChatCompletionResponse response;
  try {
    response = provider.ChatCompletion(request);
  } catch (const std::exception& ex) {
    stop_server();
    FAIL() << "ChatCompletion threw: " << ex.what();
  }

  stop_server();

  EXPECT_EQ(response.content, "copilot ok");
  EXPECT_EQ(seen_auth, "Bearer copilot-runtime-token");
  EXPECT_EQ(seen_user_agent, "GithubCopilot/1.155.0");
  EXPECT_EQ(seen_editor_version, "vscode/1.85.1");
  EXPECT_EQ(seen_editor_plugin_version, "copilot/1.155.0");
  EXPECT_EQ(resolver->resolve_calls, 1);
}

}  // namespace quantclaw
