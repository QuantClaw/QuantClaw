// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/cooldown_tracker.hpp"

#include <algorithm>

namespace quantclaw {

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

  // Failure window decay: if the last failure was more than 24h ago,
  // reset the consecutive failure counter so backoff restarts from base.
  if (state.consecutive_failures > 0 &&
      now - state.last_failure_at > kFailureWindowDecay) {
    state.consecutive_failures = 0;
  }

  state.consecutive_failures++;
  state.last_error = kind;
  state.last_failure_at = now;
  state.last_probe_at =
      now;  // Reset probe timer so first probe waits kProbeInterval

  // Prefer server-provided Retry-After over computed backoff
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
    return false;  // Not in cooldown, no probe needed

  auto now = std::chrono::steady_clock::now();
  if (now >= it->second.cooldown_until)
    return false;  // Cooldown expired

  // Check if enough time has passed since the last probe
  if (now - it->second.last_probe_at < kProbeInterval) {
    return false;  // Too soon to probe again
  }

  it->second.last_probe_at = now;
  return true;
}

std::chrono::seconds CooldownTracker::ComputeCooldown(ProviderErrorKind kind,
                                                      int failure_count) {
  // Base cooldown per error type (seconds)
  int base_s = 0;
  int cap_s = 0;

  switch (kind) {
    case ProviderErrorKind::kRateLimit:
      // 60s → 300s → 1500s → 3600s cap
      base_s = 60;
      cap_s = 3600;
      break;

    case ProviderErrorKind::kAuthError:
      // Fixed 3600s (1 hour) — re-authentication unlikely to help soon
      return std::chrono::seconds(3600);

    case ProviderErrorKind::kBillingError:
      // 5h → 24h cap
      base_s = 18000;  // 5 hours
      cap_s = 86400;   // 24 hours
      break;

    case ProviderErrorKind::kTransient:
      // 60s → 300s → 1500s → 3600s cap (same as rate limit)
      base_s = 60;
      cap_s = 3600;
      break;

    case ProviderErrorKind::kModelNotFound:
      // No cooldown — fall back immediately
      return std::chrono::seconds(0);

    case ProviderErrorKind::kTimeout:
      // 30s → 60s → 120s → 300s cap
      base_s = 30;
      cap_s = 300;
      break;

    case ProviderErrorKind::kContextOverflow:
      // Context overflow should fall through to the next model immediately
      // instead of putting the current one into cooldown.
      return std::chrono::seconds(0);

    case ProviderErrorKind::kUnknown:
      base_s = 60;
      cap_s = 600;
      break;
  }

  if (base_s == 0)
    return std::chrono::seconds(0);

  // Exponential backoff: base * 5^(failures-1), capped
  // failure_count: 1 → base, 2 → base*5, 3 → base*25, ...
  int multiplier = 1;
  for (int i = 1; i < failure_count && i < 5; ++i) {
    multiplier *= 5;
  }

  int cooldown = std::min(base_s * multiplier, cap_s);
  return std::chrono::seconds(cooldown);
}

}  // namespace quantclaw
