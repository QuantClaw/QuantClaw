// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/command_queue.hpp"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>

namespace quantclaw::gateway {

// ================================================================
// Enum conversions
// ================================================================

QueueMode QueueModeFromString(const std::string& s) {
  if (s == "collect")
    return QueueMode::kCollect;
  if (s == "followup")
    return QueueMode::kFollowup;
  if (s == "steer")
    return QueueMode::kSteer;
  if (s == "steer-backlog")
    return QueueMode::kSteerBacklog;
  if (s == "interrupt")
    return QueueMode::kInterrupt;
  return QueueMode::kCollect;
}

std::string QueueModeToString(QueueMode mode) {
  switch (mode) {
    case QueueMode::kCollect:
      return "collect";
    case QueueMode::kFollowup:
      return "followup";
    case QueueMode::kSteer:
      return "steer";
    case QueueMode::kSteerBacklog:
      return "steer-backlog";
    case QueueMode::kInterrupt:
      return "interrupt";
  }
  return "collect";
}

DropPolicy DropPolicyFromString(const std::string& s) {
  if (s == "summarize")
    return DropPolicy::kSummarize;
  if (s == "drop-oldest")
    return DropPolicy::kDropOldest;
  if (s == "reject")
    return DropPolicy::kReject;
  return DropPolicy::kSummarize;
}

std::string DropPolicyToString(DropPolicy policy) {
  switch (policy) {
    case DropPolicy::kSummarize:
      return "summarize";
    case DropPolicy::kDropOldest:
      return "drop-oldest";
    case DropPolicy::kReject:
      return "reject";
  }
  return "summarize";
}

// ================================================================
// QueueConfig
// ================================================================

QueueConfig QueueConfig::FromJson(const nlohmann::json& json) {
  QueueConfig c;
  c.max_concurrent = json.value("maxConcurrent", 4);
  c.debounce_ms = json.value("debounceMs", 1000);
  c.cap = json.value("cap", 20);
  c.drop = DropPolicyFromString(json.value("drop", "summarize"));
  c.default_mode = QueueModeFromString(json.value("defaultMode", "collect"));
  return c;
}

nlohmann::json QueueConfig::ToJson() const {
  return {
      {"maxConcurrent", max_concurrent},
      {"debounceMs", debounce_ms},
      {"cap", cap},
      {"drop", DropPolicyToString(drop)},
      {"defaultMode", QueueModeToString(default_mode)},
  };
}

// ================================================================
// SessionLane
// ================================================================

SessionLane::SessionLane(const std::string& session_key)
    : session_key_(session_key) {}

void SessionLane::Enqueue(QueuedCommand cmd) {
  pending_.push_back(std::move(cmd));
}

bool SessionLane::HasPending() const {
  return !pending_.empty();
}

std::optional<QueuedCommand>
SessionLane::TryActivate(std::chrono::steady_clock::time_point now) {
  if (pending_.empty() || active_command_)
    return std::nullopt;

  auto& front = pending_.front();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - front.enqueued_at);
  if (elapsed.count() < debounce_ms_)
    return std::nullopt;

  QueuedCommand cmd = std::move(front);
  pending_.pop_front();
  cmd.state = QueuedCommand::State::kActive;
  active_command_ = std::move(cmd);
  return active_command_;
}

std::optional<QueuedCommand> SessionLane::CompleteActive() {
  if (!active_command_)
    return std::nullopt;
  auto cmd = std::move(*active_command_);
  cmd.state = QueuedCommand::State::kComplete;
  active_command_.reset();
  return cmd;
}

std::vector<std::string> SessionLane::ApplyCapOverflow() {
  std::vector<std::string> dropped_ids;
  while (static_cast<int>(pending_.size()) > cap_) {
    switch (drop_) {
      case DropPolicy::kSummarize: {
        if (pending_.size() >= 2) {
          // Merge oldest messages into a single summary entry.
          // Collapse the oldest two: keep snippets of each, prefix
          // with count so the agent knows how many messages were condensed.
          auto& oldest = pending_[0];
          auto& second = pending_[1];

          // Count how many messages were already summarized
          int collapsed_count = 1;
          if (oldest.message.find("[Queue summary:") == 0) {
            // Extract existing count from "[Queue summary: N messages] ..."
            auto pos = oldest.message.find(" messages]");
            if (pos != std::string::npos) {
              auto start = oldest.message.find(": ") + 2;
              try {
                collapsed_count =
                    std::stoi(oldest.message.substr(start, pos - start));
              } catch (...) {}
            }
          }
          collapsed_count++;  // +1 for the newly merged message

          // Build summary with truncated snippets
          std::string summary =
              "[Queue summary: " + std::to_string(collapsed_count) +
              " messages] ";
          // Keep the core content from the existing oldest (may already be a
          // summary)
          if (oldest.message.find("[Queue summary:") == 0) {
            auto content_start = oldest.message.find("] ");
            if (content_start != std::string::npos) {
              summary += oldest.message.substr(content_start + 2);
            }
          } else {
            summary += oldest.message.substr(0, 150);
          }
          summary += " | ";
          summary += second.message.substr(0, 150);

          oldest.message = summary;
          dropped_ids.push_back(second.id);
          pending_.erase(pending_.begin() + 1);
        } else {
          // Only one item but over cap (shouldn't happen with cap>=1)
          break;
        }
        break;
      }
      case DropPolicy::kDropOldest: {
        dropped_ids.push_back(pending_.front().id);
        pending_.pop_front();
        break;
      }
      case DropPolicy::kReject: {
        dropped_ids.push_back(pending_.back().id);
        pending_.pop_back();
        break;
      }
    }
    // Safety: avoid infinite loop if cap is 0 or other edge case
    if (cap_ <= 0)
      break;
  }
  return dropped_ids;
}

std::string SessionLane::DrainPendingAsSteeringText() {
  if (pending_.empty())
    return "";
  std::string result;
  for (auto& cmd : pending_) {
    if (!result.empty())
      result += "\n\n";
    result += cmd.message;
  }
  pending_.clear();
  return result;
}

bool SessionLane::CancelPending(const std::string& command_id) {
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if (it->id == command_id) {
      pending_.erase(it);
      return true;
    }
  }
  return false;
}

std::optional<std::string> SessionLane::InterruptActive() {
  std::optional<std::string> active_id;
  if (active_command_) {
    active_id = active_command_->id;
    active_command_.reset();
  }
  // Clear all pending except the most recent (the interrupt message)
  while (pending_.size() > 1) {
    pending_.pop_front();
  }
  return active_id;
}

nlohmann::json SessionLane::ToJson() const {
  return {
      {"sessionKey", session_key_},
      {"mode", QueueModeToString(mode_)},
      {"debounceMs", debounce_ms_},
      {"cap", cap_},
      {"drop", DropPolicyToString(drop_)},
      {"pendingCount", pending_.size()},
      {"hasActive", active_command_.has_value()},
  };
}

// ================================================================
// CommandQueue
// ================================================================

static std::string generate_uuid() {
  thread_local static std::random_device rd;
  thread_local static std::mt19937 gen(rd());
  thread_local static std::uniform_int_distribution<uint32_t> dist;

  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  uint32_t a = dist(gen), b = dist(gen), c = dist(gen), d = dist(gen);
  ss << std::setw(8) << a << "-" << std::setw(4) << (b >> 16) << "-"
     << std::setw(4) << (b & 0xFFFF) << "-" << std::setw(4) << (c >> 16) << "-"
     << std::setw(4) << (c & 0xFFFF) << std::setw(8) << d;
  return ss.str();
}

CommandQueue::CommandQueue(const QueueConfig& config, AgentExecutor executor,
                           ResponseSender response_sender,
                           EventSender event_sender,
                           std::shared_ptr<spdlog::logger> logger)
    : config_(config),
      executor_(std::move(executor)),
      response_sender_(std::move(response_sender)),
      event_sender_(std::move(event_sender)),
      logger_(std::move(logger)) {}

CommandQueue::~CommandQueue() {
  Stop();
}

void CommandQueue::Start() {
  if (running_)
    return;
  running_ = true;
  dispatcher_ = std::thread([this] { dispatcher_loop(); });
  logger_->info("CommandQueue started (maxConcurrent={})",
                config_.max_concurrent);
}

void CommandQueue::Stop() {
  if (!running_)
    return;
  running_ = false;
  cv_.notify_all();
  if (dispatcher_.joinable()) {
    dispatcher_.join();
  }
  // Join all worker threads to avoid use-after-free.
  std::vector<std::thread> workers_to_join;
  {
    std::lock_guard<std::mutex> lock(mu_);
    workers_to_join.swap(workers_);
  }
  for (auto& w : workers_to_join) {
    if (w.joinable()) {
      w.join();
    }
  }
  logger_->info("CommandQueue stopped");
}

std::string CommandQueue::generate_id() const {
  return "cmd-" + generate_uuid();
}

SessionLane& CommandQueue::get_lane(const std::string& session_key) {
  auto it = lanes_.find(session_key);
  if (it == lanes_.end()) {
    auto lane = std::make_unique<SessionLane>(session_key);
    lane->SetMode(config_.default_mode);
    lane->SetDebounceMs(config_.debounce_ms);
    lane->SetCap(config_.cap);
    lane->SetDropPolicy(config_.drop);
    auto [iter, _] = lanes_.emplace(session_key, std::move(lane));
    return *iter->second;
  }
  return *it->second;
}

std::string CommandQueue::Submit(const std::string& session_key,
                                 const std::string& message,
                                 const nlohmann::json& params,
                                 const std::string& connection_id,
                                 const std::string& rpc_request_id,
                                 QueueMode mode) {
  std::string cmd_id = generate_id();

  QueuedCommand cmd;
  cmd.id = cmd_id;
  cmd.session_key = session_key;
  cmd.message = message;
  cmd.params = params;
  cmd.connection_id = connection_id;
  cmd.rpc_request_id = rpc_request_id;
  cmd.mode = mode;
  cmd.enqueued_at = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(mu_);
    auto& lane = get_lane(session_key);

    // Use session-level mode if command didn't specify one
    if (mode == QueueMode::kCollect) {
      cmd.mode = lane.GetMode();
    }

    // Handle interrupt mode: clear lane
    if (cmd.mode == QueueMode::kInterrupt && lane.HasActive()) {
      auto aborted_id = lane.InterruptActive();
      if (aborted_id) {
        logger_->info("Interrupted active command {} for session {}",
                      *aborted_id, session_key);
      }
    }

    // Handle steer mode: if active, inject as steering text
    if (cmd.mode == QueueMode::kSteer && lane.HasActive()) {
      // For steer, we add to pending but the dispatcher will
      // drain it as steering text for the active run
      lane.Enqueue(std::move(cmd));
      command_to_session_[cmd_id] = session_key;
      cv_.notify_one();
      return cmd_id;
    }

    // Handle steer-backlog: steer if active, normal queue if idle
    if (cmd.mode == QueueMode::kSteerBacklog && lane.HasActive()) {
      lane.Enqueue(std::move(cmd));
      command_to_session_[cmd_id] = session_key;
      cv_.notify_one();
      return cmd_id;
    }

    lane.Enqueue(std::move(cmd));
    command_to_session_[cmd_id] = session_key;

    // Apply cap overflow
    auto dropped = lane.ApplyCapOverflow();
    if (!dropped.empty()) {
      for (const auto& did : dropped) {
        logger_->warn("Queue overflow: collapsed command {} for session {}",
                      did, session_key);
        command_to_session_.erase(did);
      }
      // Notify the client about the overflow summarization
      event_sender_(connection_id, "queue.overflow",
                    {
                        {"sessionKey", session_key},
                        {"droppedCount", dropped.size()},
                        {"policy", DropPolicyToString(lane.GetDropPolicy())},
                        {"pendingCount", lane.PendingCount()},
                    });
    }
  }

  cv_.notify_one();
  logger_->debug("Queued command {} for session {}", cmd_id, session_key);
  return cmd_id;
}

bool CommandQueue::Cancel(const std::string& command_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = command_to_session_.find(command_id);
  if (it == command_to_session_.end())
    return false;

  auto& lane = get_lane(it->second);
  bool cancelled = lane.CancelPending(command_id);
  command_to_session_.erase(it);
  return cancelled;
}

bool CommandQueue::AbortSession(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = lanes_.find(session_key);
  if (it == lanes_.end())
    return false;

  auto aborted_id = it->second->InterruptActive();
  if (aborted_id) {
    command_to_session_.erase(*aborted_id);
    logger_->info("Aborted active command for session {}", session_key);
    return true;
  }
  return false;
}

void CommandQueue::ConfigureSession(const std::string& session_key,
                                    QueueMode mode, int debounce_ms, int cap,
                                    const std::string& drop) {
  std::lock_guard<std::mutex> lock(mu_);
  auto& lane = get_lane(session_key);
  lane.SetMode(mode);
  if (debounce_ms >= 0)
    lane.SetDebounceMs(debounce_ms);
  if (cap >= 0)
    lane.SetCap(cap);
  if (!drop.empty())
    lane.SetDropPolicy(DropPolicyFromString(drop));
}

nlohmann::json
CommandQueue::SessionQueueStatus(const std::string& session_key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = lanes_.find(session_key);
  if (it == lanes_.end()) {
    return {{"sessionKey", session_key}, {"exists", false}};
  }
  auto status = it->second->ToJson();
  status["exists"] = true;
  return status;
}

nlohmann::json CommandQueue::GlobalStatus() const {
  std::lock_guard<std::mutex> lock(mu_);
  int total_pending = 0;
  int active_lanes = 0;
  for (const auto& [key, lane] : lanes_) {
    total_pending += static_cast<int>(lane->PendingCount());
    if (lane->HasActive())
      active_lanes++;
  }
  return {
      {"config", config_.ToJson()},          {"activeLanes", active_lanes},
      {"activeCount", active_count_.load()}, {"totalPending", total_pending},
      {"totalLanes", lanes_.size()},
  };
}

void CommandQueue::SetConfig(const QueueConfig& config) {
  std::lock_guard<std::mutex> lock(mu_);
  config_ = config;
}

void CommandQueue::dispatcher_loop() {
  while (running_) {
    std::unique_lock<std::mutex> lock(mu_);

    cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
      if (!running_)
        return true;
      if (active_count_ >= config_.max_concurrent)
        return false;
      for (auto& [key, lane] : lanes_) {
        if (!lane->HasActive() && lane->HasPending())
          return true;
      }
      return false;
    });

    if (!running_)
      break;

    auto now = std::chrono::steady_clock::now();

    // Collect session keys first to avoid iterator invalidation.
    // (Releasing mu_ mid-iteration would let other threads modify lanes_.)
    std::vector<std::string> session_keys;
    session_keys.reserve(lanes_.size());
    for (const auto& [key, _] : lanes_) {
      session_keys.push_back(key);
    }

    // Collect commands to dispatch without releasing the lock.
    std::vector<QueuedCommand> to_dispatch;

    for (const auto& key : session_keys) {
      auto it = lanes_.find(key);
      if (it == lanes_.end())
        continue;
      auto& lane = it->second;

      if (lane->HasActive())
        continue;
      if (active_count_ >= config_.max_concurrent)
        break;

      // For collect mode: try to activate, but also collect
      // any additional pending messages
      auto cmd_opt = lane->TryActivate(now);
      if (!cmd_opt)
        continue;

      QueuedCommand command = std::move(*cmd_opt);

      // Collect mode: batch remaining pending messages
      if (command.mode == QueueMode::kCollect && lane->HasPending()) {
        std::string steering = lane->DrainPendingAsSteeringText();
        if (!steering.empty()) {
          command.params["collectedMessages"] = steering;
        }
      }

      // Followup mode: keep only the last pending
      if (command.mode == QueueMode::kFollowup) {
        // The command we just activated is the one to run.
        // Discard everything else in pending.
        if (lane->PendingCount() > 1) {
          lane->DrainPendingAsSteeringText();
        }
      }

      active_count_++;
      command_to_session_.erase(command.id);
      to_dispatch.push_back(std::move(command));
    }

    // Clean up completed workers to prevent unbounded growth.
    workers_.erase(std::remove_if(workers_.begin(), workers_.end(),
                                  [](std::thread& t) {
                                    if (!t.joinable())
                                      return true;
                                    return false;
                                  }),
                   workers_.end());

    // Clean up idle lanes (no active, no pending, avoid map bloat)
    for (auto it = lanes_.begin(); it != lanes_.end();) {
      if (it->second->IsIdle()) {
        it = lanes_.erase(it);
      } else {
        ++it;
      }
    }

    // Release the lock, then dispatch worker threads.
    lock.unlock();

    for (auto& cmd : to_dispatch) {
      std::lock_guard<std::mutex> wlock(mu_);
      workers_.emplace_back([this, c = std::move(cmd)]() mutable {
        execute_command(std::move(c));
      });
    }
  }
}

void CommandQueue::execute_command(QueuedCommand cmd) {
  logger_->debug("Executing command {} for session {}", cmd.id,
                 cmd.session_key);

  emit_queue_event(cmd, "queue.started");

  try {
    auto event_fn =
        [this, conn_id = cmd.connection_id](const std::string& event_name,
                                            const nlohmann::json& payload) {
          event_sender_(conn_id, event_name, payload);
        };

    auto result = executor_(cmd, event_fn);

    if (!cmd.rpc_request_id.empty()) {
      response_sender_(cmd.connection_id, cmd.rpc_request_id, true, result);
    }
  } catch (const std::exception& e) {
    logger_->error("Command {} failed: {}", cmd.id, e.what());
    if (!cmd.rpc_request_id.empty()) {
      response_sender_(cmd.connection_id, cmd.rpc_request_id, false,
                       {{"error", e.what()}});
    }
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = lanes_.find(cmd.session_key);
    if (it != lanes_.end()) {
      it->second->CompleteActive();
    }
    active_count_--;
  }

  emit_queue_event(cmd, "queue.completed");
  cv_.notify_one();
}

void CommandQueue::emit_queue_event(const QueuedCommand& cmd,
                                    const std::string& event_type,
                                    const nlohmann::json& data) {
  nlohmann::json payload = {
      {"commandId", cmd.id},
      {"sessionKey", cmd.session_key},
  };
  if (!data.empty()) {
    payload.merge_patch(data);
  }
  event_sender_(cmd.connection_id, event_type, payload);
}

}  // namespace quantclaw::gateway
