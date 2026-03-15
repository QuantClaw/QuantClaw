// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace quantclaw {

// Temporal decay scoring based on file modification time.
//
// Uses exponential decay: score = exp(−λ * age_days)
// where λ = ln(2) / half_life_days.
//
// Default half-life: 30 days (score ≈ 0.5 after 30 days).
class TemporalDecay {
 public:
  explicit TemporalDecay(double half_life_days = 30.0);

  // Compute decay score for a file based on its modification time.
  // Returns a value in (0, 1] where 1.0 = just modified.
  double Score(const std::filesystem::path& filepath) const;

  // Compute decay score given a time point.
  double Score(std::chrono::system_clock::time_point mtime) const;

  // Compute decay score given age in days.
  double ScoreFromAge(double age_days) const;

  // Get the configured half-life in days.
  double HalfLifeDays() const {
    return half_life_days_;
  }

 private:
  double half_life_days_;
  double lambda_;  // ln(2) / half_life_days
};

}  // namespace quantclaw
