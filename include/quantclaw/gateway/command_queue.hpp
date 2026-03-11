// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/common/noncopyable.hpp"

namespace quantclaw::gateway {

// ================================================================
// QueueMode: determines how new messages interact with an
// existing in-progress run for the same session.
// ================================================================

enum class QueueMode {
  kCollect,       // Default. Buffer messages; deliver all when
                  // current run finishes (batched follow-up).
  kFollowup,      // Queue one follow-up that fires after current run.
  kSteer,         // Inject new message into running context at
                  // the next tool boundary.
  kSteerBacklog,  // Like steer when active; queue normally if idle.
  kInterrupt,     // Abort the current run, start with new message.
};

QueueMode QueueModeFromString(const std::string& s);
std::string QueueModeToString(QueueMode mode);

// ================================================================
// DropPolicy: what happens when the queue exceeds its cap.
// ================================================================

enum class DropPolicy {
  kSummarize,   // Collapse oldest queued messages into a summary
  kDropOldest,  // Discard the oldest queued message
  kReject,      // Reject the new submission
};

DropPolicy DropPolicyFromString(const std::string& s);
std::string DropPolicyToString(DropPolicy policy);

// ================================================================
// QueueConfig: global defaults, overridable per session.
// ================================================================

struct QueueConfig {
  int max_concurrent = 4;
  int debounce_ms = 1000;
  int cap = 20;
  DropPolicy drop = DropPolicy::kSummarize;
  QueueMode default_mode = QueueMode::kCollect;

  static QueueConfig FromJson(const nlohmann::json& json);
  nlohmann::json ToJson() const;
};

// ================================================================
// QueuedCommand: a single submitted message.
// ================================================================

struct QueuedCommand {
  std::string id;
  std::string session_key;
  std::string message;
  nlohmann::json params;
  std::string connection_id;
  std::string rpc_request_id;
  QueueMode mode;
  std::chrono::steady_clock::time_point enqueued_at;

  enum class State {
    kPending,
    kDebouncing,
    kActive,
    kComplete,
    kDropped,
  };
  State state = State::kPending;
};

// ================================================================
// SessionLane: per-session queue that enforces serialization.
// At most one command is active per lane at any time.
// ================================================================

class SessionLane {
 public:
  explicit SessionLane(const std::string& session_key);

  const std::string& SessionKey() const {
    return session_key_;
  }

  // Per-session config overrides
  void SetMode(QueueMode mode) {
    mode_ = mode;
  }
  QueueMode GetMode() const {
    return mode_;
  }
  void SetDebounceMs(int ms) {
    debounce_ms_ = ms;
  }
  int GetDebounceMs() const {
    return debounce_ms_;
  }
  void SetCap(int cap) {
    cap_ = cap;
  }
  int GetCap() const {
    return cap_;
  }
  void SetDropPolicy(DropPolicy policy) {
    drop_ = policy;
  }
  DropPolicy GetDropPolicy() const {
    return drop_;
  }

  // Queue management (caller holds CommandQueue's mutex)
  void Enqueue(QueuedCommand cmd);
  bool HasPending() const;
  bool HasActive() const {
    return active_command_.has_value();
  }
  const QueuedCommand& ActiveCommand() const {
    return *active_command_;
  }

  // Pop the next pending command and mark it active.
  // Returns nullopt if no pending commands or debounce
  // timer hasn't expired.
  std::optional<QueuedCommand>
  TryActivate(std::chrono::steady_clock::time_point now);

  // Mark the active command as complete and return it.
  std::optional<QueuedCommand> CompleteActive();

  // Apply cap overflow policy. Returns dropped command IDs.
  std::vector<std::string> ApplyCapOverflow();

  // For steer mode: drain all pending messages as a single
  // concatenated string, clearing the pending queue.
  std::string DrainPendingAsSteeringText();

  // Cancel a specific pending command by ID.
  // Returns true if the command was found and removed.
  bool CancelPending(const std::string& command_id);

  // For interrupt: clear pending + abort active.
  // Returns the active command's ID if one was running.
  std::optional<std::string> InterruptActive();

  // Queue introspection
  size_t PendingCount() const {
    return pending_.size();
  }
  bool IsIdle() const {
    return !HasActive() && !HasPending();
  }

  nlohmann::json ToJson() const;

 private:
  std::string session_key_;
  QueueMode mode_ = QueueMode::kCollect;
  int debounce_ms_ = 1000;
  int cap_ = 20;
  DropPolicy drop_ = DropPolicy::kSummarize;

  std::deque<QueuedCommand> pending_;
  std::optional<QueuedCommand> active_command_;
};

// ================================================================
// Callback types for CommandQueue.
// ================================================================

// Executes an agent request. Called on a worker thread.
// Must block until the agent run completes.
using AgentExecutor = std::function<nlohmann::json(
    const QueuedCommand& cmd,
    std::function<void(const std::string& event, const nlohmann::json& payload)>
        event_sink)>;

// Sends a final RPC response to a client.
using ResponseSender = std::function<void(
    const std::string& connection_id, const std::string& rpc_request_id,
    bool ok, const nlohmann::json& payload_or_error)>;

// Sends an RPC event to a specific client.
using EventSender = std::function<void(const std::string& connection_id,
                                       const std::string& event_name,
                                       const nlohmann::json& payload)>;

// ================================================================
// CommandQueue: central queue manager.
// Runs a dispatcher thread that picks work from lanes and
// dispatches it to workers, respecting per-session serialization
// and global concurrency limits.
// ================================================================

class CommandQueue : public quantclaw::Noncopyable {
 public:
  CommandQueue(const QueueConfig& config, AgentExecutor executor,
               ResponseSender response_sender, EventSender event_sender,
               std::shared_ptr<spdlog::logger> logger);
  ~CommandQueue();

  // Start the dispatcher thread.
  void Start();

  // Stop all processing. Waits for active runs to finish.
  void Stop();

  // Submit a new command. Returns the command ID.
  // Thread-safe: called from any RPC handler thread.
  std::string Submit(const std::string& session_key, const std::string& message,
                     const nlohmann::json& params,
                     const std::string& connection_id,
                     const std::string& rpc_request_id, QueueMode mode);

  // Cancel a specific queued command (not yet active).
  bool Cancel(const std::string& command_id);

  // Abort the active run for a session.
  bool AbortSession(const std::string& session_key);

  // Per-session configuration override.
  void ConfigureSession(const std::string& session_key, QueueMode mode,
                        int debounce_ms = -1, int cap = -1,
                        const std::string& drop = "");

  // Query queue state.
  nlohmann::json SessionQueueStatus(const std::string& session_key) const;
  nlohmann::json GlobalStatus() const;

  // Update global config.
  void SetConfig(const QueueConfig& config);

 private:
  std::string generate_id() const;
  SessionLane& get_lane(const std::string& session_key);
  void dispatcher_loop();
  void execute_command(QueuedCommand cmd);
  void emit_queue_event(const QueuedCommand& cmd, const std::string& event_type,
                        const nlohmann::json& data = {});

  QueueConfig config_;
  AgentExecutor executor_;
  ResponseSender response_sender_;
  EventSender event_sender_;
  std::shared_ptr<spdlog::logger> logger_;

  mutable std::mutex mu_;
  std::condition_variable cv_;

  // session_key -> lane
  std::unordered_map<std::string, std::unique_ptr<SessionLane>> lanes_;

  // Dispatcher thread
  std::thread dispatcher_;
  std::atomic<bool> running_{false};
  std::atomic<int> active_count_{0};

  // Worker threads (tracked so we can join them in Stop())
  std::vector<std::thread> workers_;

  // cmd_id -> session_key (for Cancel lookup)
  std::unordered_map<std::string, std::string> command_to_session_;
};

}  // namespace quantclaw::gateway
