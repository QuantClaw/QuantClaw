// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/usage_accumulator.hpp"

namespace quantclaw {

void UsageAccumulator::Record(const std::string& session_key, int input_tokens,
                              int output_tokens) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& s = sessions_[session_key];
  s.input_tokens += input_tokens;
  s.output_tokens += output_tokens;
  s.total_tokens += input_tokens + output_tokens;
  s.turns++;

  global_.input_tokens += input_tokens;
  global_.output_tokens += output_tokens;
  global_.total_tokens += input_tokens + output_tokens;
  global_.turns++;
}

UsageAccumulator::Stats
UsageAccumulator::GetSession(const std::string& session_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_key);
  if (it == sessions_.end())
    return {};
  return it->second;
}

UsageAccumulator::Stats UsageAccumulator::GetGlobal() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return global_;
}

void UsageAccumulator::ResetSession(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(session_key);
}

void UsageAccumulator::ResetAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear();
  global_ = {};
}

nlohmann::json UsageAccumulator::ToJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json j;
  j["global"] = {{"inputTokens", global_.input_tokens},
                 {"outputTokens", global_.output_tokens},
                 {"totalTokens", global_.total_tokens},
                 {"turns", global_.turns}};
  nlohmann::json sessions = nlohmann::json::object();
  for (const auto& [key, s] : sessions_) {
    sessions[key] = {{"inputTokens", s.input_tokens},
                     {"outputTokens", s.output_tokens},
                     {"totalTokens", s.total_tokens},
                     {"turns", s.turns}};
  }
  j["sessions"] = sessions;
  return j;
}

}  // namespace quantclaw
