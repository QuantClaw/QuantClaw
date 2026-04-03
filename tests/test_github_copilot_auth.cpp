// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#define test_setenv(name, value) _putenv_s(name, value)
#define test_unsetenv(name) _putenv_s(name, "")
#else
#define test_setenv(name, value) setenv(name, value, 1)
#define test_unsetenv(name) unsetenv(name)
#endif

#include <httplib.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/auth/github_copilot_auth.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw::auth {
namespace {

std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, sink);
}

class FakeTokenClient : public GitHubCopilotTokenClient {
 public:
  explicit FakeTokenClient(std::shared_ptr<spdlog::logger> logger)
      : GitHubCopilotTokenClient(std::move(logger)) {}

  GitHubCopilotRuntimeCredential result;
  int exchange_calls = 0;

  GitHubCopilotRuntimeCredential
  ExchangeForApiToken(const std::string& github_token) override {
    ++exchange_calls;
    last_github_token = github_token;
    return result;
  }

  std::string last_github_token;
};

}  // namespace

TEST(GitHubCopilotAuthTest, StoreRoundTripsRecord) {
  const auto dir = test::MakeTestDir("github_copilot_auth_store");
  GitHubCopilotAuthStore store(dir / "github-copilot.json");

  GitHubCopilotAuthRecord record;
  record.provider = "github-copilot";
  record.access_token = "github-access";
  record.account_id = "github-user";
  record.expires_at = 4102444800;

  store.Save(record);
  const auto loaded = store.Load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->provider, "github-copilot");
  EXPECT_EQ(loaded->access_token, "github-access");
  EXPECT_EQ(loaded->account_id, "github-user");
}

TEST(GitHubCopilotAuthTest, RuntimeResolverCachesUsableApiToken) {
  const auto dir = test::MakeTestDir("github_copilot_runtime_cache");
  GitHubCopilotAuthStore store(dir / "github-copilot.json");

  GitHubCopilotAuthRecord record;
  record.provider = "github-copilot";
  record.access_token = "github-access";
  record.expires_at = 4102444800;
  store.Save(record);

  auto logger = make_logger("github-copilot-runtime-cache");
  auto client = std::make_shared<FakeTokenClient>(logger);
  client->result.api_token = "copilot-api-token";
  client->result.base_url = "https://api.individual.githubcopilot.com";
  client->result.expires_at = 4102444800;

  GitHubCopilotRuntimeResolver resolver(
      store, GitHubCopilotTokenCache(dir / "github-copilot-runtime.json"),
      client, logger);

  const auto first = resolver.ResolveRuntimeCredential(4000000000);
  const auto second = resolver.ResolveRuntimeCredential(4000000001);

  EXPECT_EQ(first.api_token, "copilot-api-token");
  EXPECT_EQ(first.base_url, "https://api.individual.githubcopilot.com");
  EXPECT_EQ(second.api_token, "copilot-api-token");
  EXPECT_EQ(client->exchange_calls, 1);
  EXPECT_EQ(client->last_github_token, "github-access");
}

TEST(GitHubCopilotAuthTest, RuntimeResolverPrefersEnvironmentTokens) {
  test_unsetenv("COPILOT_GITHUB_TOKEN");
  test_unsetenv("GH_TOKEN");
  test_unsetenv("GITHUB_TOKEN");

  const auto dir = test::MakeTestDir("github_copilot_runtime_env");
  GitHubCopilotAuthStore store(dir / "github-copilot.json");
  auto logger = make_logger("github-copilot-runtime-env");
  auto client = std::make_shared<FakeTokenClient>(logger);
  client->result.api_token = "copilot-api-token";
  client->result.base_url = "https://api.individual.githubcopilot.com";
  client->result.expires_at = 4102444800;

  GitHubCopilotRuntimeResolver resolver(
      store, GitHubCopilotTokenCache(dir / "github-copilot-runtime.json"),
      client, logger, [] { return "from-copilot-token"; });

  const auto result = resolver.ResolveRuntimeCredential(4000000000);

  EXPECT_EQ(result.api_token, "copilot-api-token");
  EXPECT_EQ(client->exchange_calls, 1);
  EXPECT_EQ(client->last_github_token, "from-copilot-token");
}

TEST(GitHubCopilotAuthTest, RuntimeResolverPrefersEnvironmentTokenOverCache) {
  const auto dir = test::MakeTestDir("github_copilot_runtime_env_over_cache");
  GitHubCopilotTokenCache cache(dir / "github-copilot-runtime.json");
  GitHubCopilotRuntimeCredential cached;
  cached.api_token = "cached-api-token";
  cached.base_url = "https://cached.example";
  cached.expires_at = 4102444800;
  cache.Save(cached);

  auto logger = make_logger("github-copilot-runtime-env-over-cache");
  auto client = std::make_shared<FakeTokenClient>(logger);
  client->result.api_token = "fresh-api-token";
  client->result.base_url = "https://fresh.example";
  client->result.expires_at = 4102444800;

  GitHubCopilotRuntimeResolver resolver(
      GitHubCopilotAuthStore(dir / "github-copilot.json"), cache, client,
      logger, [] { return "env-token"; });

  const auto result = resolver.ResolveRuntimeCredential(4000000000);

  EXPECT_EQ(result.api_token, "fresh-api-token");
  EXPECT_EQ(result.base_url, "https://fresh.example");
  EXPECT_EQ(client->exchange_calls, 1);
  EXPECT_EQ(client->last_github_token, "env-token");
}

TEST(GitHubCopilotAuthTest, TokenExchangeSendsUserAgentHeader) {
  const int port = test::FindFreePort();
  ASSERT_GT(port, 0);

  httplib::Server server;
  std::string seen_auth;
  std::string seen_user_agent;

  server.Get("/copilot_internal/v2/token", [&](const httplib::Request& req,
                                               httplib::Response& res) {
    seen_auth = req.get_header_value("Authorization");
    seen_user_agent = req.get_header_value("User-Agent");
    res.set_content(R"({"token":"copilot-api-token","expires_at":4102444800})",
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
    FAIL() << "Timed out waiting for mock Copilot server";
  }

  auto logger = make_logger("github-copilot-token-user-agent");
  GitHubCopilotTokenClient client(logger,
                                  "http://127.0.0.1:" + std::to_string(port) +
                                      "/copilot_internal/v2/token");

  GitHubCopilotRuntimeCredential credential;
  try {
    credential = client.ExchangeForApiToken("github-access");
  } catch (const std::exception& ex) {
    stop_server();
    FAIL() << "ExchangeForApiToken threw: " << ex.what();
  }

  stop_server();

  EXPECT_EQ(credential.api_token, "copilot-api-token");
  EXPECT_EQ(seen_auth, "Bearer github-access");
  EXPECT_FALSE(seen_user_agent.empty());
}

}  // namespace quantclaw::auth
