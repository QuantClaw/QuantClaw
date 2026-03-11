// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace quantclaw {

// Tracks cumulative token usage per session and globally.
// Thread-safe: all methods can be called from any thread.
class UsageAccumulator {
 public:
  struct Stats {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t total_tokens = 0;
    int turns = 0;  // Number of LLM calls
  };

  // Record a single LLM call's token usage.
  void Record(const std::string& session_key, int input_tokens,
              int output_tokens);

  // Get usage for a specific session.
  Stats GetSession(const std::string& session_key) const;

  // Get global (all sessions) usage.
  Stats GetGlobal() const;

  // Reset usage for a specific session.
  void ResetSession(const std::string& session_key);

  // Reset all usage.
  void ResetAll();

  // Serialize to JSON.
  nlohmann::json ToJson() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Stats> sessions_;
  Stats global_;
};

}  // namespace quantclaw
