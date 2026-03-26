// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.temporal_decay;

import std;

export namespace quantclaw {

class TemporalDecay {
 public:
  explicit TemporalDecay(double half_life_days = 30.0);

  double Score(const std::filesystem::path& filepath) const;
  double Score(std::chrono::system_clock::time_point mtime) const;
  double ScoreFromAge(double age_days) const;

  double HalfLifeDays() const {
    return half_life_days_;
  }

 private:
  double half_life_days_;
  double lambda_;
};

}  // namespace quantclaw