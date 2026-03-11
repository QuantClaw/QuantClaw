// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/gateway/command_queue.hpp"

#include <gtest/gtest.h>

using namespace quantclaw::gateway;

// ================================================================
// QueueMode / DropPolicy enum conversion tests
// ================================================================

TEST(QueueModeTest, RoundTrip) {
  EXPECT_EQ(QueueModeFromString("collect"), QueueMode::kCollect);
  EXPECT_EQ(QueueModeFromString("followup"), QueueMode::kFollowup);
  EXPECT_EQ(QueueModeFromString("steer"), QueueMode::kSteer);
  EXPECT_EQ(QueueModeFromString("steer-backlog"), QueueMode::kSteerBacklog);
  EXPECT_EQ(QueueModeFromString("interrupt"), QueueMode::kInterrupt);
  EXPECT_EQ(QueueModeFromString("unknown"), QueueMode::kCollect);

  EXPECT_EQ(QueueModeToString(QueueMode::kCollect), "collect");
  EXPECT_EQ(QueueModeToString(QueueMode::kFollowup), "followup");
  EXPECT_EQ(QueueModeToString(QueueMode::kSteer), "steer");
  EXPECT_EQ(QueueModeToString(QueueMode::kSteerBacklog), "steer-backlog");
  EXPECT_EQ(QueueModeToString(QueueMode::kInterrupt), "interrupt");
}

TEST(DropPolicyTest, RoundTrip) {
  EXPECT_EQ(DropPolicyFromString("summarize"), DropPolicy::kSummarize);
  EXPECT_EQ(DropPolicyFromString("drop-oldest"), DropPolicy::kDropOldest);
  EXPECT_EQ(DropPolicyFromString("reject"), DropPolicy::kReject);
  EXPECT_EQ(DropPolicyFromString("unknown"), DropPolicy::kSummarize);

  EXPECT_EQ(DropPolicyToString(DropPolicy::kSummarize), "summarize");
  EXPECT_EQ(DropPolicyToString(DropPolicy::kDropOldest), "drop-oldest");
  EXPECT_EQ(DropPolicyToString(DropPolicy::kReject), "reject");
}

// ================================================================
// QueueConfig tests
// ================================================================

TEST(QueueConfigTest, FromJsonDefaults) {
  auto config = QueueConfig::FromJson(nlohmann::json::object());
  EXPECT_EQ(config.max_concurrent, 4);
  EXPECT_EQ(config.debounce_ms, 1000);
  EXPECT_EQ(config.cap, 20);
  EXPECT_EQ(config.drop, DropPolicy::kSummarize);
  EXPECT_EQ(config.default_mode, QueueMode::kCollect);
}

TEST(QueueConfigTest, FromJsonCustom) {
  nlohmann::json j = {{"maxConcurrent", 8},
                      {"debounceMs", 500},
                      {"cap", 10},
                      {"drop", "reject"},
                      {"defaultMode", "followup"}};
  auto config = QueueConfig::FromJson(j);
  EXPECT_EQ(config.max_concurrent, 8);
  EXPECT_EQ(config.debounce_ms, 500);
  EXPECT_EQ(config.cap, 10);
  EXPECT_EQ(config.drop, DropPolicy::kReject);
  EXPECT_EQ(config.default_mode, QueueMode::kFollowup);
}

TEST(QueueConfigTest, ToJsonRoundTrip) {
  QueueConfig config;
  config.max_concurrent = 2;
  config.debounce_ms = 200;
  config.cap = 5;
  config.drop = DropPolicy::kDropOldest;
  config.default_mode = QueueMode::kSteer;

  auto j = config.ToJson();
  auto restored = QueueConfig::FromJson(j);
  EXPECT_EQ(restored.max_concurrent, 2);
  EXPECT_EQ(restored.debounce_ms, 200);
  EXPECT_EQ(restored.cap, 5);
  EXPECT_EQ(restored.drop, DropPolicy::kDropOldest);
  EXPECT_EQ(restored.default_mode, QueueMode::kSteer);
}

// ================================================================
// SessionLane tests
// ================================================================

class SessionLaneTest : public ::testing::Test {
 protected:
  void SetUp() override {
    lane_ = std::make_unique<SessionLane>("test-session");
    lane_->SetDebounceMs(0);  // No debounce for unit tests
    lane_->SetCap(5);
  }

  QueuedCommand MakeCmd(const std::string& id, const std::string& msg) {
    QueuedCommand cmd;
    cmd.id = id;
    cmd.session_key = "test-session";
    cmd.message = msg;
    cmd.mode = QueueMode::kCollect;
    cmd.enqueued_at = std::chrono::steady_clock::now();
    return cmd;
  }

  std::unique_ptr<SessionLane> lane_;
};

TEST_F(SessionLaneTest, InitialState) {
  EXPECT_EQ(lane_->SessionKey(), "test-session");
  EXPECT_FALSE(lane_->HasPending());
  EXPECT_FALSE(lane_->HasActive());
  EXPECT_TRUE(lane_->IsIdle());
  EXPECT_EQ(lane_->PendingCount(), 0u);
}

TEST_F(SessionLaneTest, EnqueueAndActivate) {
  lane_->Enqueue(MakeCmd("cmd-1", "hello"));
  EXPECT_TRUE(lane_->HasPending());
  EXPECT_EQ(lane_->PendingCount(), 1u);
  EXPECT_FALSE(lane_->HasActive());

  auto now = std::chrono::steady_clock::now();
  auto activated = lane_->TryActivate(now);
  ASSERT_TRUE(activated.has_value());
  EXPECT_EQ(activated->id, "cmd-1");
  EXPECT_EQ(activated->state, QueuedCommand::State::kActive);

  EXPECT_TRUE(lane_->HasActive());
  EXPECT_FALSE(lane_->HasPending());
  EXPECT_FALSE(lane_->IsIdle());
}

TEST_F(SessionLaneTest, CannotActivateWhileActive) {
  lane_->Enqueue(MakeCmd("cmd-1", "first"));
  lane_->Enqueue(MakeCmd("cmd-2", "second"));

  auto now = std::chrono::steady_clock::now();
  auto first = lane_->TryActivate(now);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->id, "cmd-1");

  // Second should not activate while first is active
  auto second = lane_->TryActivate(now);
  EXPECT_FALSE(second.has_value());
}

TEST_F(SessionLaneTest, CompleteActive) {
  lane_->Enqueue(MakeCmd("cmd-1", "hello"));
  auto now = std::chrono::steady_clock::now();
  lane_->TryActivate(now);

  auto completed = lane_->CompleteActive();
  ASSERT_TRUE(completed.has_value());
  EXPECT_EQ(completed->id, "cmd-1");
  EXPECT_EQ(completed->state, QueuedCommand::State::kComplete);

  EXPECT_FALSE(lane_->HasActive());
}

TEST_F(SessionLaneTest, CompleteActiveWhenNoneActive) {
  auto completed = lane_->CompleteActive();
  EXPECT_FALSE(completed.has_value());
}

TEST_F(SessionLaneTest, DebounceRespectsTimer) {
  lane_->SetDebounceMs(100);  // 100ms debounce
  auto cmd = MakeCmd("cmd-1", "hello");
  cmd.enqueued_at = std::chrono::steady_clock::now();
  lane_->Enqueue(std::move(cmd));

  // Immediately after enqueue, should not activate
  auto now = std::chrono::steady_clock::now();
  auto result = lane_->TryActivate(now);
  EXPECT_FALSE(result.has_value());

  // After debounce period, should activate
  std::this_thread::sleep_for(std::chrono::milliseconds(110));
  now = std::chrono::steady_clock::now();
  result = lane_->TryActivate(now);
  EXPECT_TRUE(result.has_value());
}

TEST_F(SessionLaneTest, CancelPending) {
  lane_->Enqueue(MakeCmd("cmd-1", "first"));
  lane_->Enqueue(MakeCmd("cmd-2", "second"));
  lane_->Enqueue(MakeCmd("cmd-3", "third"));
  EXPECT_EQ(lane_->PendingCount(), 3u);

  // Cancel middle command
  EXPECT_TRUE(lane_->CancelPending("cmd-2"));
  EXPECT_EQ(lane_->PendingCount(), 2u);

  // Cancel nonexistent
  EXPECT_FALSE(lane_->CancelPending("cmd-999"));
  EXPECT_EQ(lane_->PendingCount(), 2u);

  // Activate and verify order
  auto now = std::chrono::steady_clock::now();
  auto first = lane_->TryActivate(now);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->id, "cmd-1");

  lane_->CompleteActive();
  auto third = lane_->TryActivate(now);
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->id, "cmd-3");
}

TEST_F(SessionLaneTest, DrainPendingAsSteeringText) {
  lane_->Enqueue(MakeCmd("cmd-1", "first message"));
  lane_->Enqueue(MakeCmd("cmd-2", "second message"));

  auto text = lane_->DrainPendingAsSteeringText();
  EXPECT_EQ(text, "first message\n\nsecond message");
  EXPECT_FALSE(lane_->HasPending());
  EXPECT_EQ(lane_->PendingCount(), 0u);
}

TEST_F(SessionLaneTest, DrainPendingEmpty) {
  auto text = lane_->DrainPendingAsSteeringText();
  EXPECT_TRUE(text.empty());
}

TEST_F(SessionLaneTest, InterruptActive) {
  lane_->Enqueue(MakeCmd("cmd-1", "first"));
  lane_->Enqueue(MakeCmd("cmd-2", "second"));

  auto now = std::chrono::steady_clock::now();
  lane_->TryActivate(now);
  EXPECT_TRUE(lane_->HasActive());

  // Add interrupt message
  lane_->Enqueue(MakeCmd("cmd-3", "interrupt msg"));

  auto aborted_id = lane_->InterruptActive();
  ASSERT_TRUE(aborted_id.has_value());
  EXPECT_EQ(*aborted_id, "cmd-1");
  EXPECT_FALSE(lane_->HasActive());
  // Only the most recent pending should remain
  EXPECT_EQ(lane_->PendingCount(), 1u);
}

TEST_F(SessionLaneTest, InterruptNoActive) {
  auto aborted_id = lane_->InterruptActive();
  EXPECT_FALSE(aborted_id.has_value());
}

// --- Cap overflow policies ---

TEST_F(SessionLaneTest, CapOverflow_DropOldest) {
  lane_->SetCap(3);
  lane_->SetDropPolicy(DropPolicy::kDropOldest);

  for (int i = 0; i < 5; i++) {
    lane_->Enqueue(
        MakeCmd("cmd-" + std::to_string(i), "msg-" + std::to_string(i)));
  }

  auto dropped = lane_->ApplyCapOverflow();
  EXPECT_EQ(dropped.size(), 2u);
  EXPECT_EQ(dropped[0], "cmd-0");
  EXPECT_EQ(dropped[1], "cmd-1");
  EXPECT_EQ(lane_->PendingCount(), 3u);
}

TEST_F(SessionLaneTest, CapOverflow_Reject) {
  lane_->SetCap(3);
  lane_->SetDropPolicy(DropPolicy::kReject);

  for (int i = 0; i < 5; i++) {
    lane_->Enqueue(
        MakeCmd("cmd-" + std::to_string(i), "msg-" + std::to_string(i)));
  }

  auto dropped = lane_->ApplyCapOverflow();
  EXPECT_EQ(dropped.size(), 2u);
  EXPECT_EQ(dropped[0], "cmd-4");
  EXPECT_EQ(dropped[1], "cmd-3");
  EXPECT_EQ(lane_->PendingCount(), 3u);
}

TEST_F(SessionLaneTest, CapOverflow_Summarize) {
  lane_->SetCap(3);
  lane_->SetDropPolicy(DropPolicy::kSummarize);

  for (int i = 0; i < 5; i++) {
    lane_->Enqueue(
        MakeCmd("cmd-" + std::to_string(i), "message-" + std::to_string(i)));
  }

  auto dropped = lane_->ApplyCapOverflow();
  EXPECT_FALSE(dropped.empty());
  // After summarization, should be at or below cap
  EXPECT_LE(static_cast<int>(lane_->PendingCount()), 3);
}

TEST_F(SessionLaneTest, CapNotExceeded) {
  lane_->SetCap(10);
  lane_->Enqueue(MakeCmd("cmd-1", "msg"));
  lane_->Enqueue(MakeCmd("cmd-2", "msg"));

  auto dropped = lane_->ApplyCapOverflow();
  EXPECT_TRUE(dropped.empty());
  EXPECT_EQ(lane_->PendingCount(), 2u);
}

// --- ToJson ---

TEST_F(SessionLaneTest, ToJson) {
  lane_->SetMode(QueueMode::kSteer);
  lane_->Enqueue(MakeCmd("cmd-1", "hello"));

  auto j = lane_->ToJson();
  EXPECT_EQ(j["sessionKey"], "test-session");
  EXPECT_EQ(j["mode"], "steer");
  EXPECT_EQ(j["pendingCount"], 1);
  EXPECT_FALSE(j["hasActive"].get<bool>());
}

// ================================================================
// CommandQueue integration tests
// ================================================================

class CommandQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("cq_test", null_sink);

    QueueConfig config;
    config.max_concurrent = 2;
    config.debounce_ms = 0;  // No debounce for tests
    config.cap = 10;

    queue_ = std::make_unique<CommandQueue>(
        config,
        [this](const QueuedCommand& cmd,
               std::function<void(const std::string&, const nlohmann::json&)>)
            -> nlohmann::json {
          std::unique_lock<std::mutex> lock(exec_mu_);
          exec_count_++;
          last_executed_msg_ = cmd.message;
          last_executed_session_ = cmd.session_key;
          exec_cv_.notify_all();

          // Simulate work - wait for signal if blocking is enabled
          if (block_execution_) {
            exec_cv_.wait(lock,
                          [this] { return !block_execution_ || !running_; });
          }

          return {{"status", "ok"}, {"message", cmd.message}};
        },
        [this](const std::string& conn_id, const std::string& req_id, bool ok,
               const nlohmann::json& payload) {
          std::lock_guard<std::mutex> lock(resp_mu_);
          responses_.push_back({conn_id, req_id, ok, payload});
          resp_cv_.notify_all();
        },
        [this](const std::string& conn_id, const std::string& event_name,
               const nlohmann::json& payload) {
          std::lock_guard<std::mutex> lock(event_mu_);
          events_.push_back({conn_id, event_name, payload});
          event_cv_.notify_all();
        },
        logger_);
  }

  void TearDown() override {
    running_ = false;
    block_execution_ = false;
    exec_cv_.notify_all();
    if (queue_) {
      queue_->Stop();
    }
  }

  bool WaitForExecCount(int expected, int timeout_ms = 2000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> lock(exec_mu_);
    return exec_cv_.wait_until(lock, deadline,
                               [&] { return exec_count_ >= expected; });
  }

  struct ResponseRecord {
    std::string conn_id;
    std::string req_id;
    bool ok;
    nlohmann::json payload;
  };

  struct EventRecord {
    std::string conn_id;
    std::string event_name;
    nlohmann::json payload;
  };

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<CommandQueue> queue_;

  std::mutex exec_mu_;
  std::condition_variable exec_cv_;
  std::atomic<int> exec_count_{0};
  std::string last_executed_msg_;
  std::string last_executed_session_;
  std::atomic<bool> block_execution_{false};
  std::atomic<bool> running_{true};

  std::mutex resp_mu_;
  std::condition_variable resp_cv_;
  std::vector<ResponseRecord> responses_;

  std::mutex event_mu_;
  std::condition_variable event_cv_;
  std::vector<EventRecord> events_;
};

TEST_F(CommandQueueTest, SubmitAndExecute) {
  queue_->Start();

  auto id = queue_->Submit("session-1", "hello world", {}, "conn-1", "req-1",
                           QueueMode::kCollect);
  EXPECT_FALSE(id.empty());
  EXPECT_TRUE(id.find("cmd-") == 0);

  EXPECT_TRUE(WaitForExecCount(1));
  EXPECT_EQ(last_executed_msg_, "hello world");
  EXPECT_EQ(last_executed_session_, "session-1");
}

TEST_F(CommandQueueTest, PerSessionSerialization) {
  block_execution_ = true;
  queue_->Start();

  // Submit two commands to the same session
  queue_->Submit("session-1", "first", {}, "conn-1", "req-1",
                 QueueMode::kCollect);

  EXPECT_TRUE(WaitForExecCount(1));
  EXPECT_EQ(last_executed_msg_, "first");

  // Second command should not execute while first is active
  queue_->Submit("session-1", "second", {}, "conn-1", "req-2",
                 QueueMode::kCollect);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(exec_count_.load(), 1);

  // Release the first command
  block_execution_ = false;
  exec_cv_.notify_all();

  // Second should now execute
  EXPECT_TRUE(WaitForExecCount(2));
  EXPECT_EQ(last_executed_msg_, "second");
}

TEST_F(CommandQueueTest, DifferentSessionsRunConcurrently) {
  block_execution_ = true;
  queue_->Start();

  queue_->Submit("session-1", "msg-1", {}, "conn-1", "", QueueMode::kCollect);
  queue_->Submit("session-2", "msg-2", {}, "conn-2", "", QueueMode::kCollect);

  // Both should execute concurrently (max_concurrent=2)
  EXPECT_TRUE(WaitForExecCount(2));

  block_execution_ = false;
  exec_cv_.notify_all();
}

TEST_F(CommandQueueTest, GlobalConcurrencyLimit) {
  block_execution_ = true;
  QueueConfig config;
  config.max_concurrent = 1;  // Only 1 concurrent
  config.debounce_ms = 0;
  queue_->SetConfig(config);
  queue_->Start();

  queue_->Submit("session-1", "msg-1", {}, "conn-1", "", QueueMode::kCollect);
  queue_->Submit("session-2", "msg-2", {}, "conn-2", "", QueueMode::kCollect);

  EXPECT_TRUE(WaitForExecCount(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Only 1 should have executed due to limit
  EXPECT_EQ(exec_count_.load(), 1);

  block_execution_ = false;
  exec_cv_.notify_all();
  EXPECT_TRUE(WaitForExecCount(2));
}

TEST_F(CommandQueueTest, Cancel) {
  block_execution_ = true;
  queue_->Start();

  // Submit and block first command
  queue_->Submit("session-1", "first", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  // Submit second, it should be pending
  auto id2 = queue_->Submit("session-1", "second", {}, "conn-1", "",
                            QueueMode::kCollect);

  // Cancel the pending one
  EXPECT_TRUE(queue_->Cancel(id2));

  // Release first
  block_execution_ = false;
  exec_cv_.notify_all();

  // Give time for dispatcher to process - second should NOT execute
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(exec_count_.load(), 1);
}

TEST_F(CommandQueueTest, AbortSession) {
  block_execution_ = true;
  queue_->Start();

  queue_->Submit("session-1", "first", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  bool aborted = queue_->AbortSession("session-1");
  EXPECT_TRUE(aborted);

  block_execution_ = false;
  exec_cv_.notify_all();
}

TEST_F(CommandQueueTest, AbortNonexistentSession) {
  queue_->Start();
  EXPECT_FALSE(queue_->AbortSession("no-such-session"));
}

TEST_F(CommandQueueTest, SessionQueueStatus) {
  queue_->Start();

  // Nonexistent session
  auto status = queue_->SessionQueueStatus("session-1");
  EXPECT_FALSE(status["exists"].get<bool>());

  // Create a session by submitting
  queue_->Submit("session-1", "hello", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  status = queue_->SessionQueueStatus("session-1");
  EXPECT_EQ(status["sessionKey"], "session-1");
}

TEST_F(CommandQueueTest, GlobalStatus) {
  queue_->Start();

  auto status = queue_->GlobalStatus();
  EXPECT_EQ(status["activeLanes"], 0);
  EXPECT_EQ(status["totalPending"], 0);
  EXPECT_TRUE(status.contains("config"));
}

TEST_F(CommandQueueTest, ConfigureSession) {
  queue_->Start();

  queue_->ConfigureSession("session-1", QueueMode::kSteer, 500, 10, "reject");

  // Submit to verify lane was created with config
  queue_->Submit("session-1", "hello", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  auto status = queue_->SessionQueueStatus("session-1");
  EXPECT_EQ(status["mode"], "steer");
  EXPECT_EQ(status["debounceMs"], 500);
  EXPECT_EQ(status["cap"], 10);
  EXPECT_EQ(status["drop"], "reject");
}

TEST_F(CommandQueueTest, ResponseSenderCalled) {
  queue_->Start();

  queue_->Submit("session-1", "hello", {}, "conn-1", "req-42",
                 QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  // Wait for response
  {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::unique_lock<std::mutex> lock(resp_mu_);
    resp_cv_.wait_until(lock, deadline, [&] { return !responses_.empty(); });
  }

  ASSERT_FALSE(responses_.empty());
  EXPECT_EQ(responses_[0].conn_id, "conn-1");
  EXPECT_EQ(responses_[0].req_id, "req-42");
  EXPECT_TRUE(responses_[0].ok);
}

TEST_F(CommandQueueTest, EventsSentOnStartAndComplete) {
  queue_->Start();

  queue_->Submit("session-1", "hello", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  // Wait for events to arrive
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::lock_guard<std::mutex> lock(event_mu_);
  // Should have at least started + completed events
  bool has_started = false;
  bool has_completed = false;
  for (const auto& ev : events_) {
    if (ev.event_name == "queue.started")
      has_started = true;
    if (ev.event_name == "queue.completed")
      has_completed = true;
  }
  EXPECT_TRUE(has_started);
  EXPECT_TRUE(has_completed);
}

TEST_F(CommandQueueTest, StopWaitsForCompletion) {
  queue_->Start();

  queue_->Submit("session-1", "hello", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));

  // Stop should not hang
  queue_->Stop();
}

TEST_F(CommandQueueTest, DoubleStartIsNoop) {
  queue_->Start();
  queue_->Start();  // Should not crash or create extra threads

  queue_->Submit("session-1", "hello", {}, "conn-1", "", QueueMode::kCollect);
  EXPECT_TRUE(WaitForExecCount(1));
}

TEST_F(CommandQueueTest, DoubleStopIsNoop) {
  queue_->Start();
  queue_->Stop();
  queue_->Stop();  // Should not crash
}

// ================================================================
// Config parsing integration
// ================================================================

// ================================================================
// P4 — Extended SessionLane Tests
// ================================================================

TEST_F(SessionLaneTest, RejectPolicyThirdEnqueueRejected) {
  lane_->SetCap(2);
  lane_->SetDropPolicy(DropPolicy::kReject);

  lane_->Enqueue(MakeCmd("r-1", "msg1"));
  lane_->Enqueue(MakeCmd("r-2", "msg2"));
  lane_->Enqueue(MakeCmd("r-3", "msg3"));

  auto dropped = lane_->ApplyCapOverflow();
  // Reject policy drops the newest entries
  EXPECT_FALSE(dropped.empty());
  EXPECT_EQ(lane_->PendingCount(), 2u);

  // The rejected command should be r-3 (newest)
  bool found_r3 = false;
  for (const auto& id : dropped) {
    if (id == "r-3")
      found_r3 = true;
  }
  EXPECT_TRUE(found_r3);
}

TEST_F(SessionLaneTest, DropOldestPolicyDropsOldest) {
  lane_->SetCap(2);
  lane_->SetDropPolicy(DropPolicy::kDropOldest);

  lane_->Enqueue(MakeCmd("do-1", "oldest"));
  lane_->Enqueue(MakeCmd("do-2", "middle"));
  lane_->Enqueue(MakeCmd("do-3", "newest"));

  auto dropped = lane_->ApplyCapOverflow();
  EXPECT_EQ(dropped.size(), 1u);
  EXPECT_EQ(dropped[0], "do-1");
  EXPECT_EQ(lane_->PendingCount(), 2u);

  // Remaining should be do-2 and do-3
  auto now = std::chrono::steady_clock::now();
  auto first = lane_->TryActivate(now);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->id, "do-2");
}

TEST_F(SessionLaneTest, InterruptClearsActiveAndReturnsId) {
  lane_->Enqueue(MakeCmd("ia-1", "first"));
  lane_->Enqueue(MakeCmd("ia-2", "second"));
  lane_->Enqueue(MakeCmd("ia-3", "third"));

  auto now = std::chrono::steady_clock::now();
  lane_->TryActivate(now);
  EXPECT_TRUE(lane_->HasActive());

  auto aborted = lane_->InterruptActive();
  ASSERT_TRUE(aborted.has_value());
  EXPECT_EQ(*aborted, "ia-1");
  EXPECT_FALSE(lane_->HasActive());
}

TEST_F(SessionLaneTest, DrainPendingSteeringEmptyWhenNoPending) {
  auto text = lane_->DrainPendingAsSteeringText();
  EXPECT_TRUE(text.empty());
  EXPECT_EQ(text, "");
}

// ================================================================
// P4 — Extended CommandQueue Tests
// ================================================================

TEST_F(CommandQueueTest, CancelNonexistentReturnsFalse) {
  queue_->Start();
  EXPECT_FALSE(queue_->Cancel("nonexistent-cmd-id"));
}

TEST_F(CommandQueueTest, AbortNonexistentReturnsFalse) {
  queue_->Start();
  EXPECT_FALSE(queue_->AbortSession("nonexistent-session-key"));
}

// ================================================================
// Config parsing integration
// ================================================================

TEST(QueueConfigIntegration, ConfigParsesQueueSection) {
  nlohmann::json json_config = {{"queue",
                                 {{"maxConcurrent", 8},
                                  {"debounceMs", 2000},
                                  {"cap", 50},
                                  {"drop", "drop-oldest"},
                                  {"defaultMode", "steer"}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  ASSERT_FALSE(config.queue_config.is_null());
  EXPECT_EQ(config.queue_config["maxConcurrent"], 8);

  auto qc = QueueConfig::FromJson(config.queue_config);
  EXPECT_EQ(qc.max_concurrent, 8);
  EXPECT_EQ(qc.debounce_ms, 2000);
  EXPECT_EQ(qc.cap, 50);
  EXPECT_EQ(qc.drop, DropPolicy::kDropOldest);
  EXPECT_EQ(qc.default_mode, QueueMode::kSteer);
}

TEST(QueueConfigIntegration, EmptyConfigNoQueueSection) {
  auto config = quantclaw::QuantClawConfig::FromJson({});
  EXPECT_TRUE(config.queue_config.is_null());
}
