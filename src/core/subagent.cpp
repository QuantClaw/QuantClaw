// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/subagent.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace quantclaw {

// --- SpawnMode ---

std::string spawn_mode_to_string(SpawnMode m) {
  switch (m) {
    case SpawnMode::kRun:
      return "run";
    case SpawnMode::kSession:
      return "session";
  }
  return "run";
}

SpawnMode spawn_mode_from_string(const std::string& s) {
  if (s == "session")
    return SpawnMode::kSession;
  return SpawnMode::kRun;
}

// --- SubagentConfig ---

SubagentConfig SubagentConfig::FromJson(const nlohmann::json& j) {
  SubagentConfig c;
  c.max_depth = j.value("maxDepth", 5);
  c.max_children = j.value("maxChildren", 5);
  if (j.contains("allowedAgents") && j["allowedAgents"].is_array()) {
    for (const auto& item : j["allowedAgents"]) {
      if (item.is_string())
        c.allowed_agents.push_back(item.get<std::string>());
    }
  }
  return c;
}

// --- SubagentManager ---

SubagentManager::SubagentManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void SubagentManager::Configure(const SubagentConfig& config) {
  config_ = config;
}

void SubagentManager::SetAgentRunner(AgentRunFn runner) {
  agent_runner_ = std::move(runner);
}

SpawnResult SubagentManager::Spawn(const SpawnParams& params,
                                   const std::string& parent_session_key,
                                   int current_depth) {
  SpawnResult result;

  // Check depth limit
  if (current_depth >= config_.max_depth) {
    result.status = SpawnResult::kForbidden;
    result.error =
        "Max spawn depth (" + std::to_string(config_.max_depth) + ") exceeded";
    logger_->warn("Spawn rejected: {}", result.error);
    return result;
  }

  // Check active children limit
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = parent_children_.find(parent_session_key);
    if (it != parent_children_.end()) {
      int active = 0;
      for (const auto& rid : it->second) {
        auto rit = runs_.find(rid);
        if (rit != runs_.end() && rit->second.state == SubagentRun::kRunning) {
          ++active;
        }
      }
      if (active >= config_.max_children) {
        result.status = SpawnResult::kForbidden;
        result.error = "Max active children (" +
                       std::to_string(config_.max_children) + ") exceeded";
        logger_->warn("Spawn rejected: {}", result.error);
        return result;
      }
    }
  }

  // Check allowed agents
  std::string target_agent =
      params.agent_id.empty() ? "default" : params.agent_id;
  if (!config_.allowed_agents.empty()) {
    auto found = std::find(config_.allowed_agents.begin(),
                           config_.allowed_agents.end(), target_agent);
    if (found == config_.allowed_agents.end()) {
      result.status = SpawnResult::kForbidden;
      result.error = "Agent '" + target_agent + "' not in allowed list";
      logger_->warn("Spawn rejected: {}", result.error);
      return result;
    }
  }

  // Create child session and run
  std::string run_id = generate_run_id();
  std::string child_key = generate_session_key(target_agent);

  SubagentRun run;
  run.run_id = run_id;
  run.child_session_key = child_key;
  run.parent_session_key = parent_session_key;
  run.label = params.label.empty() ? params.task.substr(0, 50) : params.label;
  run.model = params.model;
  run.mode = params.mode;
  run.cleanup = params.cleanup;
  run.state = SubagentRun::kRunning;

  {
    std::lock_guard<std::mutex> lock(mu_);
    runs_[run_id] = run;
    parent_children_[parent_session_key].push_back(run_id);
  }

  logger_->info("Spawning subagent: id={}, task='{}', depth={}, parent={}",
                run_id, params.task.substr(0, 80), current_depth + 1,
                parent_session_key);

  // Build system prompt for child
  std::string extra_prompt =
      "You are a subagent (depth " + std::to_string(current_depth + 1) + "/" +
      std::to_string(config_.max_depth) + "). " + "Your task: " + params.task;

  // Launch agent run if handler is set
  if (agent_runner_) {
    try {
      auto agent_result =
          agent_runner_(child_key, params.task, params.model, extra_prompt);
      // If agent_runner returns synchronously, mark complete
      if (!agent_result.empty()) {
        CompleteRun(run_id, agent_result);
      }
    } catch (const std::exception& e) {
      FailRun(run_id, e.what());
      result.status = SpawnResult::kError;
      result.error = e.what();
      return result;
    }
  }

  result.status = SpawnResult::kAccepted;
  result.child_session_key = child_key;
  result.run_id = run_id;
  result.mode = params.mode;
  result.note = "Subagent spawned successfully";
  return result;
}

void SubagentManager::CompleteRun(const std::string& run_id,
                                  const std::string& result_summary) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = runs_.find(run_id);
  if (it != runs_.end()) {
    it->second.state = SubagentRun::kCompleted;
    it->second.result_summary = result_summary;
    logger_->info("Subagent {} completed", run_id);
  }
}

void SubagentManager::FailRun(const std::string& run_id,
                              const std::string& error) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = runs_.find(run_id);
  if (it != runs_.end()) {
    it->second.state = SubagentRun::kFailed;
    it->second.result_summary = "Error: " + error;
    logger_->error("Subagent {} failed: {}", run_id, error);
  }
}

bool SubagentManager::CancelRun(const std::string& run_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = runs_.find(run_id);
  if (it == runs_.end() || it->second.state != SubagentRun::kRunning) {
    return false;
  }
  it->second.state = SubagentRun::kCancelled;
  logger_->info("Subagent {} cancelled", run_id);
  return true;
}

std::vector<SubagentRun>
SubagentManager::ActiveChildren(const std::string& parent_session_key) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SubagentRun> result;
  auto it = parent_children_.find(parent_session_key);
  if (it == parent_children_.end())
    return result;

  for (const auto& rid : it->second) {
    auto rit = runs_.find(rid);
    if (rit != runs_.end() && rit->second.state == SubagentRun::kRunning) {
      result.push_back(rit->second);
    }
  }
  return result;
}

std::vector<SubagentRun> SubagentManager::AllRuns() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SubagentRun> result;
  for (const auto& [id, run] : runs_) {
    result.push_back(run);
  }
  return result;
}

const SubagentRun* SubagentManager::GetRun(const std::string& run_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = runs_.find(run_id);
  return it != runs_.end() ? &it->second : nullptr;
}

int SubagentManager::CleanupCompleted() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> to_remove;

  for (const auto& [id, run] : runs_) {
    if (run.cleanup && (run.state == SubagentRun::kCompleted ||
                        run.state == SubagentRun::kFailed ||
                        run.state == SubagentRun::kCancelled)) {
      to_remove.push_back(id);
    }
  }

  for (const auto& id : to_remove) {
    auto it = runs_.find(id);
    if (it != runs_.end()) {
      // Remove from parent's children list
      auto pit = parent_children_.find(it->second.parent_session_key);
      if (pit != parent_children_.end()) {
        auto& children = pit->second;
        children.erase(std::remove(children.begin(), children.end(), id),
                       children.end());
      }
      runs_.erase(it);
    }
  }

  if (!to_remove.empty()) {
    logger_->info("Cleaned up {} completed subagent runs", to_remove.size());
  }
  return static_cast<int>(to_remove.size());
}

std::string SubagentManager::generate_run_id() const {
  thread_local static std::mt19937 gen(std::random_device{}());
  thread_local static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << "sub_" << std::hex << dist(gen);
  return ss.str();
}

std::string
SubagentManager::generate_session_key(const std::string& agent_id) const {
  thread_local static std::mt19937 gen(std::random_device{}());
  thread_local static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << "agent:" << agent_id << ":subagent:" << std::hex << dist(gen);
  return ss.str();
}

}  // namespace quantclaw
