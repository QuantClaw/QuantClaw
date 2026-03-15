// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/temporal_decay.hpp"

#include <cmath>

namespace quantclaw {

TemporalDecay::TemporalDecay(double half_life_days)
    : half_life_days_(half_life_days > 0 ? half_life_days : 30.0),
      lambda_(std::log(2.0) / half_life_days_) {}

double TemporalDecay::ScoreFromAge(double age_days) const {
  if (age_days <= 0.0)
    return 1.0;
  return std::exp(-lambda_ * age_days);
}

double TemporalDecay::Score(std::chrono::system_clock::time_point mtime) const {
  auto now = std::chrono::system_clock::now();
  auto duration = now - mtime;
  double age_days =
      std::chrono::duration<double, std::ratio<86400>>(duration).count();
  return ScoreFromAge(age_days);
}

double TemporalDecay::Score(const std::filesystem::path& filepath) const {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(filepath, ec);
  if (ec)
    return 0.5;  // Default mid-score on error

  // Convert file_time to system_clock
  // C++17: use duration arithmetic relative to now
  auto file_duration = std::filesystem::file_time_type::clock::now() - ftime;
  auto sys_duration =
      std::chrono::duration_cast<std::chrono::seconds>(file_duration);
  auto mtime_sys = std::chrono::system_clock::now() - sys_duration;
  return Score(mtime_sys);
}

}  // namespace quantclaw
