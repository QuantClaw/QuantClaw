// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/rate_limiter.hpp"

namespace quantclaw {

RateLimiter::RateLimiter() : config_() {}

RateLimiter::RateLimiter(const Config& config) : config_(config) {}

bool RateLimiter::Allow(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  auto& timestamps = windows_[key];
  purge_old(timestamps, now);

  // Check window limit
  if (static_cast<int>(timestamps.size()) >= config_.max_requests) {
    return false;
  }

  // Check burst limit (requests in the last 1 second)
  if (config_.burst_max > 0) {
    auto one_sec_ago = now - std::chrono::seconds(1);
    int burst_count = 0;
    for (auto it = timestamps.rbegin(); it != timestamps.rend(); ++it) {
      if (*it >= one_sec_ago) {
        ++burst_count;
      } else {
        break;
      }
    }
    if (burst_count >= config_.burst_max) {
      return false;
    }
  }

  timestamps.push_back(now);
  return true;
}

int RateLimiter::Remaining(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  auto it = windows_.find(key);
  if (it == windows_.end()) {
    return config_.max_requests;
  }
  auto& timestamps = it->second;
  purge_old(timestamps, now);
  int used = static_cast<int>(timestamps.size());
  return std::max(0, config_.max_requests - used);
}

int RateLimiter::RetryAfter(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  auto it = windows_.find(key);
  if (it == windows_.end() || it->second.empty()) {
    return 0;
  }
  auto& timestamps = it->second;
  purge_old(timestamps, now);
  if (static_cast<int>(timestamps.size()) < config_.max_requests) {
    return 0;
  }
  // Oldest entry determines when a slot opens
  auto oldest = timestamps.front();
  auto window = std::chrono::seconds(config_.window_seconds);
  auto expires_at = oldest + window;
  if (expires_at <= now)
    return 0;
  return static_cast<int>(
             std::chrono::duration_cast<std::chrono::seconds>(expires_at - now)
                 .count()) +
         1;
}

void RateLimiter::Reset() {
  std::lock_guard<std::mutex> lock(mu_);
  windows_.clear();
}

void RateLimiter::Reset(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  windows_.erase(key);
}

void RateLimiter::Prune() {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  for (auto it = windows_.begin(); it != windows_.end();) {
    purge_old(it->second, now);
    if (it->second.empty()) {
      it = windows_.erase(it);
    } else {
      ++it;
    }
  }
}

void RateLimiter::Configure(const Config& config) {
  std::lock_guard<std::mutex> lock(mu_);
  config_ = config;
}

void RateLimiter::purge_old(std::deque<TimePoint>& timestamps,
                            TimePoint now) const {
  auto window = std::chrono::seconds(config_.window_seconds);
  auto cutoff = now - window;
  while (!timestamps.empty() && timestamps.front() < cutoff) {
    timestamps.pop_front();
  }
}

}  // namespace quantclaw
