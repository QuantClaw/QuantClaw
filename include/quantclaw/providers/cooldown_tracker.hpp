// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "quantclaw/providers/provider_error.hpp"

namespace quantclaw {

// Tracks per-key cooldown state with exponential backoff.
// Keys are typically "provider_id:profile_id" or "provider_id".
class CooldownTracker {
 public:
  // Returns true if the key is currently in cooldown.
  bool IsInCooldown(const std::string& key) const;

  // Record a failure for the given key.
  // Computes the next cooldown duration based on the error kind
  // and the number of consecutive failures.
  // If retry_after_seconds > 0, uses that as the cooldown duration
  // instead of the computed exponential backoff.
  void RecordFailure(const std::string& key, ProviderErrorKind kind,
                     int retry_after_seconds = 0);

  // Clear cooldown state for a key (e.g. after a successful request).
  void RecordSuccess(const std::string& key);

  // Get the time remaining in cooldown for a key.
  // Returns 0 if not in cooldown.
  std::chrono::seconds CooldownRemaining(const std::string& key) const;

  // Clear all cooldown state.
  void Reset();

  // Returns the number of consecutive failures for a key.
  int FailureCount(const std::string& key) const;

  // Probe throttling: returns true if a probe attempt is allowed
  // (at most once per kProbeInterval while in cooldown).
  // If allowed, updates the internal last_probe_at timestamp.
  bool TryProbe(const std::string& key);

  // Minimum interval between probe attempts for a key in cooldown.
  static constexpr std::chrono::seconds kProbeInterval{30};

 private:
  // If no failures have been recorded for this duration, consecutive
  // failure count resets to 0 on the next failure (decay window).
  static constexpr std::chrono::hours kFailureWindowDecay{24};

  struct CooldownState {
    int consecutive_failures = 0;
    ProviderErrorKind last_error = ProviderErrorKind::kUnknown;
    std::chrono::steady_clock::time_point cooldown_until;
    std::chrono::steady_clock::time_point last_failure_at;
    std::chrono::steady_clock::time_point last_probe_at;
  };

  // Compute cooldown duration based on error kind and failure count.
  static std::chrono::seconds ComputeCooldown(ProviderErrorKind kind,
                                              int failure_count);

  mutable std::mutex mu_;
  std::unordered_map<std::string, CooldownState> states_;
};

}  // namespace quantclaw
