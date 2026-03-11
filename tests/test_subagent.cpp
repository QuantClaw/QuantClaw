// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/subagent.hpp"

#include <gtest/gtest.h>

namespace quantclaw {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- SpawnMode ---

TEST(SpawnModeTest, ToFromString) {
  EXPECT_EQ(spawn_mode_to_string(SpawnMode::kRun), "run");
  EXPECT_EQ(spawn_mode_to_string(SpawnMode::kSession), "session");
  EXPECT_EQ(spawn_mode_from_string("run"), SpawnMode::kRun);
  EXPECT_EQ(spawn_mode_from_string("session"), SpawnMode::kSession);
  EXPECT_EQ(spawn_mode_from_string(""), SpawnMode::kRun);  // default
}

// --- SubagentConfig ---

TEST(SubagentConfigTest, FromJson) {
  nlohmann::json j = {
      {"maxDepth", 3},
      {"maxChildren", 2},
      {"allowedAgents", nlohmann::json::array({"agent1", "agent2"})},
  };
  auto c = SubagentConfig::FromJson(j);
  EXPECT_EQ(c.max_depth, 3);
  EXPECT_EQ(c.max_children, 2);
  EXPECT_EQ(c.allowed_agents.size(), 2);
}

TEST(SubagentConfigTest, Defaults) {
  auto c = SubagentConfig::FromJson(nlohmann::json::object());
  EXPECT_EQ(c.max_depth, 5);
  EXPECT_EQ(c.max_children, 5);
  EXPECT_TRUE(c.allowed_agents.empty());
}

// --- SubagentManager ---

class SubagentManagerTest : public ::testing::Test {
 protected:
  std::unique_ptr<SubagentManager> mgr_;

  void SetUp() override {
    mgr_ = std::make_unique<SubagentManager>(make_logger("subagent"));
    SubagentConfig config;
    config.max_depth = 3;
    config.max_children = 2;
    mgr_->Configure(config);
  }
};

TEST_F(SubagentManagerTest, SpawnBasic) {
  SpawnParams params;
  params.task = "Do something";
  params.label = "test-task";

  auto result = mgr_->Spawn(params, "parent-session", 0);
  EXPECT_EQ(result.status, SpawnResult::kAccepted);
  EXPECT_FALSE(result.child_session_key.empty());
  EXPECT_FALSE(result.run_id.empty());
  EXPECT_EQ(result.mode, SpawnMode::kRun);
}

TEST_F(SubagentManagerTest, SpawnDepthLimit) {
  SpawnParams params;
  params.task = "Too deep";

  auto result = mgr_->Spawn(params, "parent", 3);  // At max depth
  EXPECT_EQ(result.status, SpawnResult::kForbidden);
  EXPECT_TRUE(result.error.find("depth") != std::string::npos);
}

TEST_F(SubagentManagerTest, SpawnChildrenLimit) {
  SpawnParams params;
  params.task = "Task";

  // Spawn max_children (2) subagents
  auto r1 = mgr_->Spawn(params, "parent", 0);
  EXPECT_EQ(r1.status, SpawnResult::kAccepted);
  auto r2 = mgr_->Spawn(params, "parent", 0);
  EXPECT_EQ(r2.status, SpawnResult::kAccepted);

  // Third should be rejected
  auto r3 = mgr_->Spawn(params, "parent", 0);
  EXPECT_EQ(r3.status, SpawnResult::kForbidden);
  EXPECT_TRUE(r3.error.find("children") != std::string::npos);
}

TEST_F(SubagentManagerTest, SpawnAllowedAgentsFilter) {
  SubagentConfig config;
  config.max_depth = 5;
  config.max_children = 5;
  config.allowed_agents = {"agent-a", "agent-b"};
  mgr_->Configure(config);

  SpawnParams params;
  params.task = "Test";
  params.agent_id = "agent-a";
  EXPECT_EQ(mgr_->Spawn(params, "parent", 0).status, SpawnResult::kAccepted);

  params.agent_id = "agent-c";
  EXPECT_EQ(mgr_->Spawn(params, "parent", 0).status, SpawnResult::kForbidden);
}

TEST_F(SubagentManagerTest, CompleteRun) {
  SpawnParams params;
  params.task = "Test";
  auto result = mgr_->Spawn(params, "parent", 0);

  auto* run = mgr_->GetRun(result.run_id);
  ASSERT_NE(run, nullptr);
  EXPECT_EQ(run->state, SubagentRun::kRunning);

  mgr_->CompleteRun(result.run_id, "Done!");
  run = mgr_->GetRun(result.run_id);
  ASSERT_NE(run, nullptr);
  EXPECT_EQ(run->state, SubagentRun::kCompleted);
  EXPECT_EQ(run->result_summary, "Done!");
}

TEST_F(SubagentManagerTest, FailRun) {
  SpawnParams params;
  params.task = "Test";
  auto result = mgr_->Spawn(params, "parent", 0);

  mgr_->FailRun(result.run_id, "Something broke");
  auto* run = mgr_->GetRun(result.run_id);
  ASSERT_NE(run, nullptr);
  EXPECT_EQ(run->state, SubagentRun::kFailed);
}

TEST_F(SubagentManagerTest, CancelRun) {
  SpawnParams params;
  params.task = "Test";
  auto result = mgr_->Spawn(params, "parent", 0);

  EXPECT_TRUE(mgr_->CancelRun(result.run_id));
  auto* run = mgr_->GetRun(result.run_id);
  EXPECT_EQ(run->state, SubagentRun::kCancelled);

  // Can't cancel again
  EXPECT_FALSE(mgr_->CancelRun(result.run_id));
}

TEST_F(SubagentManagerTest, ActiveChildren) {
  SpawnParams params;
  params.task = "Task 1";
  auto r1 = mgr_->Spawn(params, "parent-a", 0);
  params.task = "Task 2";
  auto r2 = mgr_->Spawn(params, "parent-a", 0);
  params.task = "Task 3";
  mgr_->Spawn(params, "parent-b", 0);

  auto children = mgr_->ActiveChildren("parent-a");
  EXPECT_EQ(children.size(), 2);

  // Complete one
  mgr_->CompleteRun(r1.run_id);
  children = mgr_->ActiveChildren("parent-a");
  EXPECT_EQ(children.size(), 1);
}

TEST_F(SubagentManagerTest, CleanupCompleted) {
  SpawnParams params;
  params.task = "Task";
  params.cleanup = true;
  auto r1 = mgr_->Spawn(params, "parent", 0);
  mgr_->CompleteRun(r1.run_id);

  params.cleanup = false;
  auto r2 = mgr_->Spawn(params, "parent", 0);
  mgr_->CompleteRun(r2.run_id);

  int cleaned = mgr_->CleanupCompleted();
  EXPECT_EQ(cleaned, 1);  // Only cleanup=true runs get cleaned

  auto all = mgr_->AllRuns();
  EXPECT_EQ(all.size(), 1);  // The non-cleanup one remains
}

TEST_F(SubagentManagerTest, AgentRunner) {
  std::string received_task;
  mgr_->SetAgentRunner([&](const std::string&, const std::string& task,
                           const std::string&,
                           const std::string&) -> std::string {
    received_task = task;
    return "result from agent";
  });

  SpawnParams params;
  params.task = "Do the thing";
  auto result = mgr_->Spawn(params, "parent", 0);
  EXPECT_EQ(result.status, SpawnResult::kAccepted);
  EXPECT_EQ(received_task, "Do the thing");

  // Run should be completed since runner returned synchronously
  auto* run = mgr_->GetRun(result.run_id);
  EXPECT_EQ(run->state, SubagentRun::kCompleted);
  EXPECT_EQ(run->result_summary, "result from agent");
}

TEST_F(SubagentManagerTest, AllRuns) {
  SpawnParams params;
  params.task = "Task";
  mgr_->Spawn(params, "parent-1", 0);
  mgr_->Spawn(params, "parent-2", 0);

  auto all = mgr_->AllRuns();
  EXPECT_EQ(all.size(), 2);
}

TEST_F(SubagentManagerTest, SpawnSessionMode) {
  SpawnParams params;
  params.task = "Persistent task";
  params.mode = SpawnMode::kSession;

  auto result = mgr_->Spawn(params, "parent", 0);
  EXPECT_EQ(result.status, SpawnResult::kAccepted);
  EXPECT_EQ(result.mode, SpawnMode::kSession);
}

TEST_F(SubagentManagerTest, GetNonexistentRun) {
  auto* run = mgr_->GetRun("nonexistent");
  EXPECT_EQ(run, nullptr);
}

}  // namespace quantclaw
