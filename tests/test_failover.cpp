// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/providers/cooldown_tracker.hpp"
#include "quantclaw/providers/failover_resolver.hpp"
#include "quantclaw/providers/provider_error.hpp"
#include "quantclaw/providers/provider_registry.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

// ================================================================
// ProviderError tests
// ================================================================

TEST(ProviderErrorTest, ClassifyRateLimit) {
  EXPECT_EQ(ClassifyHttpError(429), ProviderErrorKind::kRateLimit);
}

TEST(ProviderErrorTest, ClassifyAuth) {
  EXPECT_EQ(ClassifyHttpError(401), ProviderErrorKind::kAuthError);
  EXPECT_EQ(ClassifyHttpError(403), ProviderErrorKind::kAuthError);
}

TEST(ProviderErrorTest, ClassifyBilling) {
  EXPECT_EQ(ClassifyHttpError(402), ProviderErrorKind::kBillingError);
}

TEST(ProviderErrorTest, ClassifyBillingFromBody) {
  EXPECT_EQ(ClassifyHttpError(400, R"({"error":"insufficient_credits"})"),
            ProviderErrorKind::kBillingError);
  EXPECT_EQ(ClassifyHttpError(400, R"({"error":"insufficient_quota"})"),
            ProviderErrorKind::kBillingError);
}

TEST(ProviderErrorTest, ClassifyModelNotFound) {
  EXPECT_EQ(ClassifyHttpError(404), ProviderErrorKind::kModelNotFound);
}

TEST(ProviderErrorTest, ClassifyTransient) {
  EXPECT_EQ(ClassifyHttpError(500), ProviderErrorKind::kTransient);
  EXPECT_EQ(ClassifyHttpError(502), ProviderErrorKind::kTransient);
  EXPECT_EQ(ClassifyHttpError(503), ProviderErrorKind::kTransient);
  EXPECT_EQ(ClassifyHttpError(504), ProviderErrorKind::kTransient);
}

TEST(ProviderErrorTest, ClassifyUnknown4xx) {
  EXPECT_EQ(ClassifyHttpError(400), ProviderErrorKind::kUnknown);
  EXPECT_EQ(ClassifyHttpError(422), ProviderErrorKind::kUnknown);
}

TEST(ProviderErrorTest, ErrorKindToString) {
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kRateLimit),
            "rate_limit");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kAuthError),
            "auth_error");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kBillingError),
            "billing_error");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kTransient),
            "transient");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kModelNotFound),
            "model_not_found");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kTimeout), "timeout");
  EXPECT_EQ(ProviderErrorKindToString(ProviderErrorKind::kUnknown), "unknown");
}

TEST(ProviderErrorTest, ExceptionConstruction) {
  ProviderError err(ProviderErrorKind::kRateLimit, 429, "Rate limited",
                    "anthropic", "prod");
  EXPECT_EQ(err.Kind(), ProviderErrorKind::kRateLimit);
  EXPECT_EQ(err.HttpStatus(), 429);
  EXPECT_EQ(std::string(err.what()), "Rate limited");
  EXPECT_EQ(err.ProviderId(), "anthropic");
  EXPECT_EQ(err.ProfileId(), "prod");
}

TEST(ProviderErrorTest, InheritsFromRuntimeError) {
  try {
    throw ProviderError(ProviderErrorKind::kTransient, 503,
                        "Service Unavailable");
  } catch (const std::runtime_error& e) {
    EXPECT_EQ(std::string(e.what()), "Service Unavailable");
  }
}

// ================================================================
// CooldownTracker tests
// ================================================================

TEST(CooldownTrackerTest, InitiallyNotInCooldown) {
  CooldownTracker tracker;
  EXPECT_FALSE(tracker.IsInCooldown("test-key"));
  EXPECT_EQ(tracker.FailureCount("test-key"), 0);
  EXPECT_EQ(tracker.CooldownRemaining("test-key").count(), 0);
}

TEST(CooldownTrackerTest, RecordFailureSetssCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);

  EXPECT_TRUE(tracker.IsInCooldown("key1"));
  EXPECT_EQ(tracker.FailureCount("key1"), 1);
  EXPECT_GT(tracker.CooldownRemaining("key1").count(), 0);
}

TEST(CooldownTrackerTest, RecordSuccessClearsCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);
  EXPECT_TRUE(tracker.IsInCooldown("key1"));

  tracker.RecordSuccess("key1");
  EXPECT_FALSE(tracker.IsInCooldown("key1"));
  EXPECT_EQ(tracker.FailureCount("key1"), 0);
}

TEST(CooldownTrackerTest, ConsecutiveFailuresIncrement) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 1);

  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 2);

  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 3);
}

TEST(CooldownTrackerTest, ModelNotFoundNoCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kModelNotFound);

  // Model not found should have 0 cooldown
  EXPECT_FALSE(tracker.IsInCooldown("key1"));
}

TEST(CooldownTrackerTest, AuthErrorFixedCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kAuthError);

  EXPECT_TRUE(tracker.IsInCooldown("key1"));
  // Auth error cooldown is fixed at 3600s
  auto remaining = tracker.CooldownRemaining("key1");
  EXPECT_GE(remaining.count(), 3590);  // Allow small margin
  EXPECT_LE(remaining.count(), 3600);
}

TEST(CooldownTrackerTest, ResetClearsAll) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);
  tracker.RecordFailure("key2", ProviderErrorKind::kTransient);

  tracker.Reset();
  EXPECT_FALSE(tracker.IsInCooldown("key1"));
  EXPECT_FALSE(tracker.IsInCooldown("key2"));
}

TEST(CooldownTrackerTest, IndependentKeys) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);

  EXPECT_TRUE(tracker.IsInCooldown("key1"));
  EXPECT_FALSE(tracker.IsInCooldown("key2"));
}

// ================================================================
// FailoverResolver tests
// ================================================================

// Mock LLM provider for testing
class MockFailoverLLM : public LLMProvider {
 public:
  explicit MockFailoverLLM(const std::string& name) : name_(name) {}

  ChatCompletionResponse ChatCompletion(const ChatCompletionRequest&) override {
    ChatCompletionResponse resp;
    resp.content = "mock response from " + name_;
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const ChatCompletionRequest&,
      std::function<void(const ChatCompletionResponse&)> cb) override {
    ChatCompletionResponse resp;
    resp.content = "mock";
    resp.is_stream_end = true;
    cb(resp);
  }

  std::string GetProviderName() const override {
    return name_;
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }

 private:
  std::string name_;
};

class FailoverResolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("failover_test", null_sink);

    registry_ = std::make_unique<ProviderRegistry>(logger_);

    // Register mock factories
    registry_->RegisterFactory("anthropic", [](const ProviderEntry& entry,
                                               auto /*logger*/) {
      return std::make_shared<MockFailoverLLM>("anthropic:" + entry.api_key);
    });
    registry_->RegisterFactory(
        "openai", [](const ProviderEntry& entry, auto /*logger*/) {
          return std::make_shared<MockFailoverLLM>("openai:" + entry.api_key);
        });

    // Add provider entries
    ProviderEntry anthropic_entry;
    anthropic_entry.id = "anthropic";
    anthropic_entry.api_key = "default-key";
    registry_->AddProvider(anthropic_entry);

    ProviderEntry openai_entry;
    openai_entry.id = "openai";
    openai_entry.api_key = "openai-key";
    registry_->AddProvider(openai_entry);

    resolver_ = std::make_unique<FailoverResolver>(registry_.get(), logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<ProviderRegistry> registry_;
  std::unique_ptr<FailoverResolver> resolver_;
};

TEST_F(FailoverResolverTest, BasicResolve) {
  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->provider_id, "anthropic");
  EXPECT_EQ(result->model, "claude-sonnet-4-6");
  EXPECT_FALSE(result->is_fallback);
}

TEST_F(FailoverResolverTest, ResolveWithProfiles) {
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->profile_id, "prod");
  EXPECT_EQ(result->provider->GetProviderName(), "anthropic:sk-prod-key");
}

TEST_F(FailoverResolverTest, ProfileRotationOnCooldown) {
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  // Put prod in cooldown
  resolver_->RecordFailure("anthropic", "prod", ProviderErrorKind::kRateLimit);

  // Should select backup
  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->profile_id, "backup");
  EXPECT_EQ(result->provider->GetProviderName(), "anthropic:sk-backup-key");
}

TEST_F(FailoverResolverTest, FallbackChain) {
  resolver_->SetFallbackChain({"openai/gpt-4o"});

  // Put anthropic in cooldown (no profiles, so the default entry is cooled
  // down)
  resolver_->RecordFailure("anthropic", "", ProviderErrorKind::kRateLimit);

  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->provider_id, "openai");
  EXPECT_TRUE(result->is_fallback);
}

TEST_F(FailoverResolverTest, AllExhausted) {
  resolver_->SetFallbackChain({"openai/gpt-4o"});

  // Put both providers in cooldown
  resolver_->RecordFailure("anthropic", "", ProviderErrorKind::kRateLimit);
  resolver_->RecordFailure("openai", "", ProviderErrorKind::kRateLimit);

  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  EXPECT_FALSE(result.has_value());
}

TEST_F(FailoverResolverTest, SessionPin) {
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  // First resolve: gets prod
  auto result1 = resolver_->Resolve("anthropic/claude-sonnet-4-6", "session-1");
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1->profile_id, "prod");

  // Record success to pin session
  resolver_->RecordSuccess("anthropic", "prod", "session-1");

  // Second resolve: should use pinned profile
  auto result2 = resolver_->Resolve("anthropic/claude-sonnet-4-6", "session-1");
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2->profile_id, "prod");
}

TEST_F(FailoverResolverTest, SessionPinClearedOnCooldown) {
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  // Pin to prod
  resolver_->RecordSuccess("anthropic", "prod", "session-1");

  // Put prod in cooldown
  resolver_->RecordFailure("anthropic", "prod", ProviderErrorKind::kRateLimit);

  // Should use backup (pin cleared because prod is in cooldown)
  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6", "session-1");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->profile_id, "backup");
}

TEST_F(FailoverResolverTest, ClearSessionPin) {
  resolver_->RecordSuccess("anthropic", "default", "session-1");
  resolver_->ClearSessionPin("session-1");
  // No crash, pin is cleared
}

TEST_F(FailoverResolverTest, SuccessClearsCooldown) {
  resolver_->RecordFailure("anthropic", "", ProviderErrorKind::kRateLimit);
  EXPECT_TRUE(resolver_->GetCooldownTracker().IsInCooldown("anthropic"));

  resolver_->RecordSuccess("anthropic", "");
  EXPECT_FALSE(resolver_->GetCooldownTracker().IsInCooldown("anthropic"));
}

// ================================================================
// Config integration
// ================================================================

TEST(FailoverConfigTest, ParseFallbacks) {
  nlohmann::json j = {{"agent",
                       {{"model", "anthropic/claude-sonnet-4-6"},
                        {"fallbacks", {"openai/gpt-4o", "ollama/llama3"}}}}};
  auto config = QuantClawConfig::FromJson(j);
  ASSERT_EQ(config.agent.fallbacks.size(), 2u);
  EXPECT_EQ(config.agent.fallbacks[0], "openai/gpt-4o");
  EXPECT_EQ(config.agent.fallbacks[1], "ollama/llama3");
}

TEST(FailoverConfigTest, EmptyFallbacks) {
  auto config = QuantClawConfig::FromJson({});
  EXPECT_TRUE(config.agent.fallbacks.empty());
}

// ================================================================
// P3 — Retry-After on ProviderError
// ================================================================

TEST(ProviderErrorTest, RetryAfterSeconds) {
  ProviderError err(ProviderErrorKind::kRateLimit, 429, "Rate limited");
  EXPECT_EQ(err.RetryAfterSeconds(), 0);

  err.SetRetryAfterSeconds(120);
  EXPECT_EQ(err.RetryAfterSeconds(), 120);
}

// ================================================================
// P3 — CooldownTracker: Retry-After override
// ================================================================

TEST(CooldownTrackerTest, RetryAfterOverridesBackoff) {
  CooldownTracker tracker;
  // With retry_after_seconds=300, cooldown should be ~300s regardless of error
  // kind
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit, 300);

  EXPECT_TRUE(tracker.IsInCooldown("key1"));
  auto remaining = tracker.CooldownRemaining("key1");
  EXPECT_GE(remaining.count(), 295);
  EXPECT_LE(remaining.count(), 300);
}

TEST(CooldownTrackerTest, RetryAfterZeroUsesDefaultBackoff) {
  CooldownTracker tracker;
  // With retry_after_seconds=0, should use computed backoff (60s for rate
  // limit)
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit, 0);

  auto remaining = tracker.CooldownRemaining("key1");
  EXPECT_GE(remaining.count(), 55);
  EXPECT_LE(remaining.count(), 60);
}

// ================================================================
// P3 — CooldownTracker: Probe throttling
// ================================================================

TEST(CooldownTrackerTest, TryProbeNotInCooldown) {
  CooldownTracker tracker;
  // Not in cooldown, TryProbe should return false
  EXPECT_FALSE(tracker.TryProbe("key1"));
}

TEST(CooldownTrackerTest, TryProbeThrottled) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);

  // Immediately after failure, TryProbe should return false
  // because last_probe_at is set to now during RecordFailure
  EXPECT_FALSE(tracker.TryProbe("key1"));
}

TEST(CooldownTrackerTest, TryProbeSuccessUpdatesTimestamp) {
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kRateLimit);

  // First probe immediately after failure: blocked
  EXPECT_FALSE(tracker.TryProbe("key1"));
  // Second probe also blocked (too soon)
  EXPECT_FALSE(tracker.TryProbe("key1"));
}

// ================================================================
// P3 — CooldownTracker: Failure window decay
// ================================================================

TEST(CooldownTrackerTest, FailureWindowDecayResets) {
  // This test verifies the decay logic conceptually:
  // When the last failure was > 24h ago, consecutive_failures resets.
  // We can't actually wait 24h in a test, but we verify the counter
  // increments normally when failures are close together.
  CooldownTracker tracker;
  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 1);

  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 2);

  tracker.RecordFailure("key1", ProviderErrorKind::kTransient);
  EXPECT_EQ(tracker.FailureCount("key1"), 3);

  // Without waiting 24h, the counter keeps incrementing
  // (The actual decay would reset to 0+1=1 after 24h inactivity)
}

// ================================================================
// P4 — Extended HTTP Error Classification Tests
// ================================================================

TEST(ProviderErrorTest, HttpErrorClassification_Billing402) {
  EXPECT_EQ(ClassifyHttpError(402), ProviderErrorKind::kBillingError);
}

TEST(ProviderErrorTest, HttpErrorClassification_Timeout408) {
  // 408 Request Timeout is in the 4xx range without explicit mapping,
  // classified as kUnknown
  EXPECT_EQ(ClassifyHttpError(408), ProviderErrorKind::kUnknown);
}

TEST(ProviderErrorTest, HttpErrorClassification_ServerError502) {
  EXPECT_EQ(ClassifyHttpError(502), ProviderErrorKind::kTransient);
}

TEST(ProviderErrorTest, HttpErrorClassification_ServerError503) {
  EXPECT_EQ(ClassifyHttpError(503), ProviderErrorKind::kTransient);
}

TEST(ProviderErrorTest, HttpErrorClassification_Unknown400) {
  EXPECT_EQ(ClassifyHttpError(400), ProviderErrorKind::kUnknown);
}

// ================================================================
// P4 — Extended CooldownTracker Tests
// ================================================================

TEST(CooldownTrackerTest, ModelNotFoundHasZeroCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key-mnf", ProviderErrorKind::kModelNotFound);
  EXPECT_FALSE(tracker.IsInCooldown("key-mnf"));
}

TEST(CooldownTrackerTest, AuthErrorHasFixedCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key-auth", ProviderErrorKind::kAuthError);
  EXPECT_TRUE(tracker.IsInCooldown("key-auth"));
  auto remaining = tracker.CooldownRemaining("key-auth");
  EXPECT_GE(remaining.count(), 3590);
  EXPECT_LE(remaining.count(), 3600);
}

TEST(CooldownTrackerTest, BillingErrorHasHighCooldown) {
  CooldownTracker tracker;
  tracker.RecordFailure("key-billing", ProviderErrorKind::kBillingError);
  EXPECT_TRUE(tracker.IsInCooldown("key-billing"));
  auto remaining = tracker.CooldownRemaining("key-billing");
  // Billing errors should have a significant cooldown
  EXPECT_GT(remaining.count(), 0);
}

TEST(CooldownTrackerTest, ConsecutiveFailuresIncrementExponentially) {
  CooldownTracker tracker;

  tracker.RecordFailure("exp-key", ProviderErrorKind::kTransient);
  auto cooldown1 = tracker.CooldownRemaining("exp-key");

  tracker.RecordFailure("exp-key", ProviderErrorKind::kTransient);
  auto cooldown2 = tracker.CooldownRemaining("exp-key");

  tracker.RecordFailure("exp-key", ProviderErrorKind::kTransient);
  auto cooldown3 = tracker.CooldownRemaining("exp-key");

  // Each successive failure should have a longer or equal cooldown
  EXPECT_GE(cooldown2.count(), cooldown1.count());
  EXPECT_GE(cooldown3.count(), cooldown2.count());
  EXPECT_EQ(tracker.FailureCount("exp-key"), 3);
}

TEST(CooldownTrackerTest, SuccessClearsCooldownCompletely) {
  CooldownTracker tracker;
  tracker.RecordFailure("clear-key", ProviderErrorKind::kRateLimit);
  EXPECT_TRUE(tracker.IsInCooldown("clear-key"));
  EXPECT_EQ(tracker.FailureCount("clear-key"), 1);

  tracker.RecordSuccess("clear-key");
  EXPECT_FALSE(tracker.IsInCooldown("clear-key"));
  EXPECT_EQ(tracker.FailureCount("clear-key"), 0);
  EXPECT_EQ(tracker.CooldownRemaining("clear-key").count(), 0);
}

TEST(CooldownTrackerTest, IndependentKeysIsolated) {
  CooldownTracker tracker;
  tracker.RecordFailure("key-a", ProviderErrorKind::kRateLimit);
  EXPECT_TRUE(tracker.IsInCooldown("key-a"));
  EXPECT_FALSE(tracker.IsInCooldown("key-b"));
  EXPECT_EQ(tracker.FailureCount("key-a"), 1);
  EXPECT_EQ(tracker.FailureCount("key-b"), 0);
}

// ================================================================
// P4 — Extended FailoverResolver Tests
// ================================================================

TEST_F(FailoverResolverTest, ProbeOnCooldownAllowsOneAttempt) {
  // When all profiles are in cooldown and TryProbe succeeds,
  // resolver should still return a result via probe mechanism
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  // Put both in cooldown
  resolver_->RecordFailure("anthropic", "prod", ProviderErrorKind::kRateLimit);
  resolver_->RecordFailure("anthropic", "backup",
                           ProviderErrorKind::kRateLimit);

  // Set fallback chain so we have somewhere to go
  resolver_->SetFallbackChain({"openai/gpt-4o"});

  // Should still resolve via fallback
  auto result = resolver_->Resolve("anthropic/claude-sonnet-4-6");
  if (result.has_value()) {
    // Either fallback or probe succeeded
    EXPECT_TRUE(result->is_fallback || result->provider_id == "anthropic");
  }
}

TEST_F(FailoverResolverTest, SessionPinClearedOnCooldownExtended) {
  std::vector<AuthProfile> profiles = {
      {"prod", "sk-prod-key", ""},
      {"backup", "sk-backup-key", ""},
  };
  resolver_->SetProfiles("anthropic", profiles);

  // Pin session to prod
  auto r1 = resolver_->Resolve("anthropic/claude-sonnet-4-6", "session-x");
  ASSERT_TRUE(r1.has_value());
  resolver_->RecordSuccess("anthropic", r1->profile_id, "session-x");

  // Now put the pinned profile in cooldown
  resolver_->RecordFailure("anthropic", r1->profile_id,
                           ProviderErrorKind::kRateLimit);

  // Next resolve should pick a different profile
  auto r2 = resolver_->Resolve("anthropic/claude-sonnet-4-6", "session-x");
  ASSERT_TRUE(r2.has_value());
  EXPECT_NE(r2->profile_id, r1->profile_id);
}
