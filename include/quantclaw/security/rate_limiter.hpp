// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace quantclaw {

// Sliding window rate limiter
class RateLimiter {
 public:
  struct Config {
    int max_requests = 100;   // Max requests per window
    int window_seconds = 60;  // Window size in seconds
    int burst_max = 20;       // Max burst (requests in 1 second)
  };

  RateLimiter();
  explicit RateLimiter(const Config& config);

  // Check if a request is allowed for the given key (e.g. connection_id or IP)
  // Returns true if allowed, false if rate-limited
  bool Allow(const std::string& key);

  // Get remaining requests for a key
  int Remaining(const std::string& key) const;

  // Get the number of seconds until the rate limit resets
  int RetryAfter(const std::string& key) const;

  // Clear all state
  void Reset();

  // Clear state for a specific key
  void Reset(const std::string& key);

  // Prune expired entries
  void Prune();

  // Update config
  void Configure(const Config& config);

  const Config& GetConfig() const {
    return config_;
  }

 private:
  using TimePoint = std::chrono::steady_clock::time_point;

  Config config_;
  mutable std::mutex mu_;

  // key -> list of request timestamps
  mutable std::unordered_map<std::string, std::deque<TimePoint>> windows_;

  void purge_old(std::deque<TimePoint>& timestamps, TimePoint now) const;
};

}  // namespace quantclaw
