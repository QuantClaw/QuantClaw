// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <gtest/gtest.h>

import std;
import nlohmann.json;
import quantclaw.security.scope_validator;

class ScopeValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
    validator_ = std::make_unique<quantclaw::ScopeValidator>(logger_);
  }

  void configure(const nlohmann::json& accepted,
                 const nlohmann::json& restricted = nlohmann::json::array()) {
    nlohmann::json config;
    config["accepted_targets"] = accepted;
    config["restricted_targets"] = restricted;
    validator_->Configure(config);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::ScopeValidator> validator_;
};

// --- Basic domain matching ---

TEST_F(ScopeValidatorTest, ExactDomainMatch) {
  configure({"example.com"});
  EXPECT_TRUE(validator_->IsTargetInScope("example.com"));
  EXPECT_FALSE(validator_->IsTargetInScope("other.com"));
}

TEST_F(ScopeValidatorTest, WildcardMatchesSubdomain) {
  configure({"*.example.com"});
  EXPECT_TRUE(validator_->IsTargetInScope("sub.example.com"));
  EXPECT_TRUE(validator_->IsTargetInScope("deep.sub.example.com"));
  // Wildcard also matches the base domain itself
  EXPECT_TRUE(validator_->IsTargetInScope("example.com"));
}

TEST_F(ScopeValidatorTest, WildcardDoesNotMatchUnrelated) {
  configure({"*.example.com"});
  EXPECT_FALSE(validator_->IsTargetInScope("example.org"));
  EXPECT_FALSE(validator_->IsTargetInScope("notexample.com"));
}

// --- CIDR matching ---

TEST_F(ScopeValidatorTest, CidrMatchesIpInRange) {
  configure({"10.0.0.0/24"});
  EXPECT_TRUE(validator_->IsTargetInScope("10.0.0.1"));
  EXPECT_TRUE(validator_->IsTargetInScope("10.0.0.254"));
  EXPECT_FALSE(validator_->IsTargetInScope("10.0.1.1"));
  EXPECT_FALSE(validator_->IsTargetInScope("192.168.1.1"));
}

TEST_F(ScopeValidatorTest, CidrSingleHost) {
  configure({"192.168.1.100/32"});
  EXPECT_TRUE(validator_->IsTargetInScope("192.168.1.100"));
  EXPECT_FALSE(validator_->IsTargetInScope("192.168.1.101"));
}

// --- Restricted targets override accepted ---

TEST_F(ScopeValidatorTest, RestrictedOverridesAccepted) {
  configure({"*.example.com"}, {"admin.example.com"});
  EXPECT_TRUE(validator_->IsTargetInScope("www.example.com"));
  EXPECT_TRUE(validator_->IsTargetInScope("api.example.com"));
  EXPECT_FALSE(validator_->IsTargetInScope("admin.example.com"));
}

TEST_F(ScopeValidatorTest, RestrictedWildcard) {
  configure({"*.example.com"}, {"*.internal.example.com"});
  EXPECT_TRUE(validator_->IsTargetInScope("www.example.com"));
  EXPECT_FALSE(validator_->IsTargetInScope("db.internal.example.com"));
}

// --- URL hostname extraction ---

TEST_F(ScopeValidatorTest, UrlTargetValidatedViaToolCall) {
  // URL hostname extraction happens in ValidateToolCall, not IsTargetInScope.
  // IsTargetInScope operates on bare hostnames/IPs.
  configure({"example.com"});
  nlohmann::json params_ok = {{"url", "https://example.com/path?q=1"}};
  EXPECT_TRUE(validator_->ValidateToolCall("header_analysis", params_ok).empty());

  nlohmann::json params_port = {{"url", "http://example.com:8080/api"}};
  EXPECT_TRUE(validator_->ValidateToolCall("header_analysis", params_port).empty());

  nlohmann::json params_bad = {{"url", "https://other.com/"}};
  EXPECT_FALSE(validator_->ValidateToolCall("header_analysis", params_bad).empty());
}

// --- ValidateToolCall ---

TEST_F(ScopeValidatorTest, ValidateToolCallAllowsInScope) {
  configure({"example.com"});
  nlohmann::json params = {{"target", "example.com"}};
  auto err = validator_->ValidateToolCall("port_scan", params);
  EXPECT_TRUE(err.empty());
}

TEST_F(ScopeValidatorTest, ValidateToolCallBlocksOutOfScope) {
  configure({"example.com"});
  nlohmann::json params = {{"target", "evil.com"}};
  auto err = validator_->ValidateToolCall("port_scan", params);
  EXPECT_FALSE(err.empty());
}

TEST_F(ScopeValidatorTest, ValidateToolCallChecksMultipleParamKeys) {
  configure({"example.com"});
  nlohmann::json params = {{"domain", "example.com"}, {"url", "https://evil.com/"}};
  auto err = validator_->ValidateToolCall("header_analysis", params);
  EXPECT_FALSE(err.empty());  // evil.com URL is out of scope
}

TEST_F(ScopeValidatorTest, ValidateToolCallNoTargetParams) {
  configure({"example.com"});
  nlohmann::json params = {{"severity", "high"}};
  // No target-like params — should pass (no target to validate)
  auto err = validator_->ValidateToolCall("some_tool", params);
  EXPECT_TRUE(err.empty());
}

// --- Disabled state ---

TEST_F(ScopeValidatorTest, NotEnabledWhenUnconfigured) {
  EXPECT_FALSE(validator_->IsEnabled());
}

TEST_F(ScopeValidatorTest, EnabledAfterConfigure) {
  configure({"example.com"});
  EXPECT_TRUE(validator_->IsEnabled());
}

// --- Format methods ---

TEST_F(ScopeValidatorTest, FormatTargets) {
  configure({"example.com", "*.test.com"}, {"admin.test.com"});
  auto accepted = validator_->FormatAcceptedTargets();
  auto restricted = validator_->FormatRestrictedTargets();
  EXPECT_NE(accepted.find("example.com"), std::string::npos);
  EXPECT_NE(accepted.find("*.test.com"), std::string::npos);
  EXPECT_NE(restricted.find("admin.test.com"), std::string::npos);
}
