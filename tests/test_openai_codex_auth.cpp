// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <memory>
#include <sstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/auth/openai_codex_auth.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace quantclaw::auth {
namespace {

std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, sink);
}

class FakeOAuthClient : public OpenAICodexOAuthClient {
 public:
  explicit FakeOAuthClient(std::shared_ptr<spdlog::logger> logger)
      : OpenAICodexOAuthClient(std::move(logger)) {}

  OpenAICodexAuthRecord login_result;
  OpenAICodexAuthRecord refresh_result;
  OpenAICodexAuthRecord last_refresh_arg;
  int login_calls = 0;
  int refresh_calls = 0;

  OpenAICodexAuthRecord LoginInteractive(std::istream& /*in*/,
                                         std::ostream& /*out*/) override {
    ++login_calls;
    return login_result;
  }

  OpenAICodexAuthRecord Refresh(const OpenAICodexAuthRecord& record) override {
    ++refresh_calls;
    last_refresh_arg = record;
    return refresh_result;
  }
};

}  // namespace

TEST(OpenAICodexAuthTest, BuildAuthorizeUrlIncludesExpectedParameters) {
  const std::string url = BuildOpenAICodexAuthorizeUrl(
      "state-123", "challenge-456", "http://localhost:1455/auth/callback");

  EXPECT_NE(url.find("https://auth.openai.com/oauth/authorize"),
            std::string::npos);
  EXPECT_NE(url.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann"),
            std::string::npos);
  EXPECT_NE(url.find("scope=openid%20profile%20email%20offline_access"),
            std::string::npos);
  EXPECT_NE(url.find("code_challenge=challenge-456"), std::string::npos);
  EXPECT_NE(url.find("state=state-123"), std::string::npos);
  EXPECT_NE(url.find("codex_cli_simplified_flow=true"), std::string::npos);
}

TEST(OpenAICodexAuthTest, ParseCallbackBindTargetUsesRedirectHostAndPort) {
  auto bind_target =
      ParseOpenAICodexCallbackBindTarget("http://localhost:1455/auth/callback");

  ASSERT_TRUE(bind_target.has_value());
  EXPECT_EQ(bind_target->host, "localhost");
  EXPECT_EQ(bind_target->port, 1455);
}

TEST(OpenAICodexAuthTest, ParseCallbackBindTargetSupportsIpv4Loopback) {
  auto bind_target = ParseOpenAICodexCallbackBindTarget(
      "http://127.0.0.1:18888/auth/callback");

  ASSERT_TRUE(bind_target.has_value());
  EXPECT_EQ(bind_target->host, "127.0.0.1");
  EXPECT_EQ(bind_target->port, 18888);
}

TEST(OpenAICodexAuthTest, StoreRoundTripsRecord) {
  const auto dir = test::MakeTestDir("openai_codex_auth_store");
  const auto path = dir / "openai-codex.json";

  OpenAICodexAuthStore store(path);
  OpenAICodexAuthRecord record;
  record.provider = "openai-codex";
  record.access_token = "access-token";
  record.refresh_token = "refresh-token";
  record.token_type = "Bearer";
  record.scope = "openid profile email offline_access";
  record.account_id = "acct_123";
  record.email = "user@example.com";
  record.expires_at = 4102444800;  // 2100-01-01

  store.Save(record);

  auto loaded = store.Load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->access_token, "access-token");
  EXPECT_EQ(loaded->refresh_token, "refresh-token");
  EXPECT_EQ(loaded->account_id, "acct_123");
  EXPECT_EQ(loaded->email, "user@example.com");
  EXPECT_EQ(loaded->expires_at, 4102444800);
}

TEST(OpenAICodexAuthTest, ResolverRefreshesExpiredCredential) {
  const auto dir = test::MakeTestDir("openai_codex_auth_refresh");
  const auto path = dir / "openai-codex.json";

  OpenAICodexAuthStore store(path);
  OpenAICodexAuthRecord stale;
  stale.provider = "openai-codex";
  stale.access_token = "expired-token";
  stale.refresh_token = "refresh-token";
  stale.expires_at = 10;
  store.Save(stale);

  auto logger = make_logger("openai-codex-auth-resolver");
  auto client = std::make_shared<FakeOAuthClient>(logger);
  client->refresh_result = stale;
  client->refresh_result.access_token = "fresh-token";
  client->refresh_result.expires_at = 4102444800;

  OpenAICodexCredentialResolver resolver(store, client, logger);
  const auto token = resolver.ResolveAccessToken(/*now_epoch_seconds=*/1000);

  EXPECT_EQ(token, "fresh-token");
  EXPECT_EQ(client->refresh_calls, 1);
  EXPECT_EQ(client->last_refresh_arg.access_token, "expired-token");

  auto reloaded = store.Load();
  ASSERT_TRUE(reloaded.has_value());
  EXPECT_EQ(reloaded->access_token, "fresh-token");
  EXPECT_EQ(reloaded->expires_at, 4102444800);
}

TEST(OpenAICodexAuthTest, ParseManualCodeDecodesCallbackUrl) {
  EXPECT_EQ(ParseOpenAICodexManualCode(
                "http://localhost:1455/auth/"
                "callback?code=abc%2Fdef%2Bghi%3D&state=unused"),
            "abc/def+ghi=");
}

TEST(OpenAICodexAuthTest, ParseManualCodeDecodesRawPastedCode) {
  EXPECT_EQ(ParseOpenAICodexManualCode("abc%2Fdef%2Bghi%3D"), "abc/def+ghi=");
}

TEST(OpenAICodexAuthTest, ParseManualCodeRawCodePreservesLiteralPlus) {
  // Raw auth codes are opaque tokens; a literal '+' must not be decoded as
  // a space the way query-string encoding would.
  EXPECT_EQ(ParseOpenAICodexManualCode("abc+def"), "abc+def");
}

}  // namespace quantclaw::auth
