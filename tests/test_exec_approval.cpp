// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/security/exec_approval.hpp"

#include <gtest/gtest.h>

namespace quantclaw {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- AskMode tests ---

TEST(AskModeTest, ParseStrings) {
  EXPECT_EQ(AskModeFromString("off"), AskMode::kOff);
  EXPECT_EQ(AskModeFromString("always"), AskMode::kAlways);
  EXPECT_EQ(AskModeFromString("on-miss"), AskMode::kOnMiss);
  EXPECT_EQ(AskModeFromString(""), AskMode::kOnMiss);  // default
}

TEST(AskModeTest, ToString) {
  EXPECT_EQ(AskModeToString(AskMode::kOff), "off");
  EXPECT_EQ(AskModeToString(AskMode::kAlways), "always");
  EXPECT_EQ(AskModeToString(AskMode::kOnMiss), "on-miss");
}

// --- ExecAllowlist tests ---

TEST(ExecAllowlistTest, ExactMatch) {
  ExecAllowlist al;
  al.AddPattern("ls");
  al.AddPattern("pwd");
  EXPECT_TRUE(al.Matches("ls"));
  EXPECT_TRUE(al.Matches("pwd"));
  EXPECT_FALSE(al.Matches("rm"));
}

TEST(ExecAllowlistTest, GlobMatch) {
  ExecAllowlist al;
  al.AddPattern("git *");
  al.AddPattern("npm *");
  EXPECT_TRUE(al.Matches("git status"));
  EXPECT_TRUE(al.Matches("git push origin main"));
  EXPECT_TRUE(al.Matches("npm install"));
  EXPECT_FALSE(al.Matches("rm -rf /"));
}

TEST(ExecAllowlistTest, WildcardPattern) {
  ExecAllowlist al;
  al.AddPattern("*.py");
  EXPECT_TRUE(al.Matches("test.py"));
  EXPECT_TRUE(al.Matches("script.py"));
  EXPECT_FALSE(al.Matches("test.sh"));
}

TEST(ExecAllowlistTest, QuestionMark) {
  ExecAllowlist al;
  al.AddPattern("test?.sh");
  EXPECT_TRUE(al.Matches("test1.sh"));
  EXPECT_TRUE(al.Matches("testA.sh"));
  EXPECT_FALSE(al.Matches("test12.sh"));
}

TEST(ExecAllowlistTest, LoadFromJson) {
  nlohmann::json j = nlohmann::json::array({"ls", "git *", "npm *"});
  ExecAllowlist al;
  al.LoadFromJson(j);
  EXPECT_EQ(al.Patterns().size(), 3);
  EXPECT_TRUE(al.Matches("git status"));
}

// --- ExecApprovalConfig tests ---

TEST(ExecApprovalConfigTest, FromJson) {
  nlohmann::json j = {
      {"ask", "always"},
      {"approvalTimeout", 60},
      {"askFallback", "approve"},
      {"allowlist", nlohmann::json::array({"git *", "ls"})},
      {"approvalRunningNoticeMs", 3000},
  };
  auto c = ExecApprovalConfig::FromJson(j);
  EXPECT_EQ(c.ask, AskMode::kAlways);
  EXPECT_EQ(c.timeout_seconds, 60);
  EXPECT_EQ(c.timeout_fallback, ApprovalDecision::kApproved);
  EXPECT_EQ(c.allowlist.size(), 2);
  EXPECT_EQ(c.approval_notice_ms, 3000);
}

TEST(ExecApprovalConfigTest, Defaults) {
  auto c = ExecApprovalConfig::FromJson(nlohmann::json::object());
  EXPECT_EQ(c.ask, AskMode::kOnMiss);
  EXPECT_EQ(c.timeout_seconds, 120);
  EXPECT_EQ(c.timeout_fallback, ApprovalDecision::kDenied);
}

// --- ExecApprovalManager tests ---

TEST(ExecApprovalManagerTest, AskOffAutoApproves) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kOff;
  mgr->Configure(config);

  auto decision = mgr->RequestApproval("rm -rf /");
  EXPECT_EQ(decision, ApprovalDecision::kApproved);
}

TEST(ExecApprovalManagerTest, OnMissAllowlistApproves) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kOnMiss;
  config.allowlist = {"git *", "ls"};
  mgr->Configure(config);

  EXPECT_EQ(mgr->RequestApproval("git status"), ApprovalDecision::kApproved);
  EXPECT_EQ(mgr->RequestApproval("ls"), ApprovalDecision::kApproved);
}

TEST(ExecApprovalManagerTest, OnMissNotInAllowlistDenied) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kOnMiss;
  config.allowlist = {"git *"};
  config.timeout_fallback = ApprovalDecision::kDenied;
  mgr->Configure(config);

  auto decision = mgr->RequestApproval("rm -rf /");
  EXPECT_EQ(decision, ApprovalDecision::kDenied);
}

TEST(ExecApprovalManagerTest, AlwaysRequiresApproval) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kAlways;
  config.timeout_fallback = ApprovalDecision::kDenied;
  mgr->Configure(config);

  // Even "safe" commands need approval in always mode
  auto decision = mgr->RequestApproval("ls");
  EXPECT_EQ(decision, ApprovalDecision::kDenied);  // falls back to deny
}

TEST(ExecApprovalManagerTest, HandlerApproves) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kAlways;
  mgr->Configure(config);

  mgr->SetApprovalHandler(
      [](const ApprovalRequest&) { return ApprovalDecision::kApproved; });

  auto decision = mgr->RequestApproval("rm -rf /");
  EXPECT_EQ(decision, ApprovalDecision::kApproved);
}

TEST(ExecApprovalManagerTest, HandlerDenies) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kAlways;
  mgr->Configure(config);

  mgr->SetApprovalHandler([](const ApprovalRequest& req) {
    if (req.command.find("rm") != std::string::npos) {
      return ApprovalDecision::kDenied;
    }
    return ApprovalDecision::kApproved;
  });

  EXPECT_EQ(mgr->RequestApproval("rm -rf /"), ApprovalDecision::kDenied);
  EXPECT_EQ(mgr->RequestApproval("ls"), ApprovalDecision::kApproved);
}

TEST(ExecApprovalManagerTest, ResolvedHistory) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  ExecApprovalConfig config;
  config.ask = AskMode::kOff;
  mgr->Configure(config);

  mgr->RequestApproval("cmd1");
  mgr->RequestApproval("cmd2");

  // "off" mode doesn't create pending/resolved entries
  // Let's test with always mode and handler
  config.ask = AskMode::kAlways;
  mgr->Configure(config);
  mgr->SetApprovalHandler(
      [](const ApprovalRequest&) { return ApprovalDecision::kApproved; });

  mgr->RequestApproval("cmd3");
  auto history = mgr->ResolvedHistory();
  EXPECT_GE(history.size(), 1);
}

TEST(ExecApprovalManagerTest, PruneExpired) {
  auto mgr = std::make_unique<ExecApprovalManager>(make_logger("approval"));
  mgr->PruneExpired();  // Should not crash on empty
  EXPECT_TRUE(mgr->PendingRequests().empty());
}

TEST(ExecApprovalManagerTest, DecisionToString) {
  EXPECT_EQ(ApprovalDecisionToString(ApprovalDecision::kApproved), "approved");
  EXPECT_EQ(ApprovalDecisionToString(ApprovalDecision::kDenied), "denied");
  EXPECT_EQ(ApprovalDecisionToString(ApprovalDecision::kTimeout), "timeout");
  EXPECT_EQ(ApprovalDecisionToString(ApprovalDecision::kPending), "pending");
}

}  // namespace quantclaw
