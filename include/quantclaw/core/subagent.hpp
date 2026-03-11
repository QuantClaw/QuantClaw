// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// Spawn mode (compatible with OpenClaw subagent.mode)
enum class SpawnMode {
  kRun,      // Ephemeral: auto-cleanup on completion
  kSession,  // Persistent: stays active for follow-ups
};

std::string spawn_mode_to_string(SpawnMode m);
SpawnMode spawn_mode_from_string(const std::string& s);

// Subagent spawn parameters
struct SpawnParams {
  std::string task;           // Task description for subagent
  std::string label;          // Human-readable label
  std::string agent_id;       // Target agent ID (defaults to current)
  std::string model;          // Model override (e.g. "openai/gpt-4")
  std::string thinking;       // Thinking level: off|low|medium|high
  int timeout_seconds = 300;  // Run timeout
  SpawnMode mode = SpawnMode::kRun;
  bool cleanup = true;  // Auto-delete session on completion
};

// Spawn result
struct SpawnResult {
  enum Status { kAccepted, kForbidden, kError };

  Status status = kError;
  std::string child_session_key;
  std::string run_id;
  SpawnMode mode = SpawnMode::kRun;
  std::string note;
  std::string error;
};

// Tracked child run info
struct SubagentRun {
  std::string run_id;
  std::string child_session_key;
  std::string parent_session_key;
  std::string label;
  std::string model;
  SpawnMode mode = SpawnMode::kRun;
  bool cleanup = true;

  enum State { kRunning, kCompleted, kFailed, kCancelled };
  State state = kRunning;
  std::string result_summary;
};

// Subagent configuration limits
struct SubagentConfig {
  int max_depth = 5;                        // Max spawn depth
  int max_children = 5;                     // Max active children per parent
  std::vector<std::string> allowed_agents;  // Allowed agent IDs (empty = all)

  static SubagentConfig FromJson(const nlohmann::json& j);
};

// Callback for launching agent runs on child sessions
using AgentRunFn = std::function<std::string(
    const std::string& session_key, const std::string& task,
    const std::string& model, const std::string& extra_system_prompt)>;

// Manages subagent lifecycle: spawn, track, and cleanup
class SubagentManager {
 public:
  explicit SubagentManager(std::shared_ptr<spdlog::logger> logger);

  // Configure limits
  void Configure(const SubagentConfig& config);

  // Set the agent run function (called to launch subagent)
  void SetAgentRunner(AgentRunFn runner);

  // Spawn a subagent. Returns spawn result.
  SpawnResult Spawn(const SpawnParams& params,
                    const std::string& parent_session_key,
                    int current_depth = 0);

  // Mark a run as completed with result
  void CompleteRun(const std::string& run_id,
                   const std::string& result_summary = "");

  // Mark a run as failed
  void FailRun(const std::string& run_id, const std::string& error = "");

  // Cancel a running subagent
  bool CancelRun(const std::string& run_id);

  // Get active children for a parent session
  std::vector<SubagentRun>
  ActiveChildren(const std::string& parent_session_key) const;

  // Get all tracked runs
  std::vector<SubagentRun> AllRuns() const;

  // Get a specific run
  const SubagentRun* GetRun(const std::string& run_id) const;

  // Cleanup completed ephemeral runs
  int CleanupCompleted();

  // Get current config
  const SubagentConfig& GetConfig() const {
    return config_;
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  SubagentConfig config_;
  AgentRunFn agent_runner_;

  mutable std::mutex mu_;
  std::unordered_map<std::string, SubagentRun> runs_;
  // parent_session_key → list of child run IDs
  std::unordered_map<std::string, std::vector<std::string>> parent_children_;

  std::string generate_run_id() const;
  std::string generate_session_key(const std::string& agent_id) const;
};

}  // namespace quantclaw
