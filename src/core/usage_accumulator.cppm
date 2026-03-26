// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

export module quantclaw.core.usage_accumulator;

import std;
import nlohmann.json;

export namespace quantclaw {

class UsageAccumulator {
 public:
  struct Stats {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t total_tokens = 0;
    int turns = 0;
  };

  void Record(const std::string& session_key, int input_tokens,
              int output_tokens);
  Stats GetSession(const std::string& session_key) const;
  Stats GetGlobal() const;
  void ResetSession(const std::string& session_key);
  void ResetAll();
  nlohmann::json ToJson() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Stats> sessions_;
  Stats global_;
};

}  // namespace quantclaw