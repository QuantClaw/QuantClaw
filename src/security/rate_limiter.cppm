// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.security.rate_limiter;

import std;

export namespace quantclaw {

class RateLimiter {
 public:
  struct Config {
    int max_requests = 100;
    int window_seconds = 60;
    int burst_max = 20;
  };

  RateLimiter();
  explicit RateLimiter(const Config& config);

  bool Allow(const std::string& key);
  int Remaining(const std::string& key) const;
  int RetryAfter(const std::string& key) const;
  void Reset();
  void Reset(const std::string& key);
  void Prune();
  void Configure(const Config& config);

  const Config& GetConfig() const { return config_; }

 private:
  using TimePoint = std::chrono::steady_clock::time_point;

  Config config_;
  mutable std::mutex mu_;
  mutable std::unordered_map<std::string, std::deque<TimePoint>> windows_;

  void purge_old(std::deque<TimePoint>& timestamps, TimePoint now) const;
};

}  // namespace quantclaw