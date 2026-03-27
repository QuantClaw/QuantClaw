// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.providers.cooldown_tracker;

import std;

import quantclaw.providers.provider_error;

export namespace quantclaw {

// Tracks per-key cooldown state with exponential backoff.
// Keys are typically "provider_id:profile_id" or "provider_id".
class CooldownTracker {
 public:
  bool IsInCooldown(const std::string& key) const;
  void RecordFailure(const std::string& key, ProviderErrorKind kind,
                     int retry_after_seconds = 0);
  void RecordSuccess(const std::string& key);
  std::chrono::seconds CooldownRemaining(const std::string& key) const;
  void Reset();
  int FailureCount(const std::string& key) const;
  bool TryProbe(const std::string& key);

  static constexpr std::chrono::seconds kProbeInterval{30};

 private:
  static constexpr std::chrono::hours kFailureWindowDecay{24};

  struct CooldownState {
    int consecutive_failures = 0;
    ProviderErrorKind last_error = ProviderErrorKind::kUnknown;
    std::chrono::steady_clock::time_point cooldown_until;
    std::chrono::steady_clock::time_point last_failure_at;
    std::chrono::steady_clock::time_point last_probe_at;
  };

  static std::chrono::seconds ComputeCooldown(ProviderErrorKind kind,
                                              int failure_count);

  mutable std::mutex mu_;
  std::unordered_map<std::string, CooldownState> states_;
};

bool CooldownTracker::IsInCooldown(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = states_.find(key);
  if (it == states_.end())
    return false;
  return std::chrono::steady_clock::now() < it->second.cooldown_until;
}

void CooldownTracker::RecordFailure(const std::string& key,
                                    ProviderErrorKind kind,
                                    int retry_after_seconds) {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  auto& state = states_[key];

  if (state.consecutive_failures > 0 &&
      now - state.last_failure_at > kFailureWindowDecay) {
    state.consecutive_failures = 0;
  }

  state.consecutive_failures++;
  state.last_error = kind;
  state.last_failure_at = now;
  state.last_probe_at = now;

  if (retry_after_seconds > 0) {
    state.cooldown_until = now + std::chrono::seconds(retry_after_seconds);
  } else {
    auto cooldown = ComputeCooldown(kind, state.consecutive_failures);
    state.cooldown_until = now + cooldown;
  }
}

void CooldownTracker::RecordSuccess(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  states_.erase(key);
}

std::chrono::seconds
CooldownTracker::CooldownRemaining(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = states_.find(key);
  if (it == states_.end())
    return std::chrono::seconds(0);

  auto now = std::chrono::steady_clock::now();
  if (now >= it->second.cooldown_until)
    return std::chrono::seconds(0);

  return std::chrono::duration_cast<std::chrono::seconds>(
      it->second.cooldown_until - now);
}

void CooldownTracker::Reset() {
  std::lock_guard<std::mutex> lock(mu_);
  states_.clear();
}

int CooldownTracker::FailureCount(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = states_.find(key);
  if (it == states_.end())
    return 0;
  return it->second.consecutive_failures;
}

bool CooldownTracker::TryProbe(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = states_.find(key);
  if (it == states_.end())
    return false;

  auto now = std::chrono::steady_clock::now();
  if (now >= it->second.cooldown_until)
    return false;

  if (now - it->second.last_probe_at < kProbeInterval) {
    return false;
  }

  it->second.last_probe_at = now;
  return true;
}

std::chrono::seconds CooldownTracker::ComputeCooldown(ProviderErrorKind kind,
                                                      int failure_count) {
  int base_s = 0;
  int cap_s = 0;

  switch (kind) {
    case ProviderErrorKind::kRateLimit:
      base_s = 60;
      cap_s = 3600;
      break;
    case ProviderErrorKind::kAuthError:
      return std::chrono::seconds(3600);
    case ProviderErrorKind::kBillingError:
      base_s = 18000;
      cap_s = 86400;
      break;
    case ProviderErrorKind::kTransient:
      base_s = 60;
      cap_s = 3600;
      break;
    case ProviderErrorKind::kModelNotFound:
      return std::chrono::seconds(0);
    case ProviderErrorKind::kTimeout:
      base_s = 30;
      cap_s = 300;
      break;
    case ProviderErrorKind::kContextOverflow:
      return std::chrono::seconds(0);
    case ProviderErrorKind::kUnknown:
      base_s = 60;
      cap_s = 600;
      break;
  }

  if (base_s == 0)
    return std::chrono::seconds(0);

  int multiplier = 1;
  for (int i = 1; i < failure_count && i < 5; ++i) {
    multiplier *= 5;
  }

  int cooldown = std::min(base_s * multiplier, cap_s);
  return std::chrono::seconds(cooldown);
}

}  // namespace quantclaw