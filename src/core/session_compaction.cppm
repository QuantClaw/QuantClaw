// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.session_compaction;

import std;
import nlohmann.json;
import quantclaw.providers.llm_provider;

export namespace quantclaw {

class SessionCompaction {
 public:
  explicit SessionCompaction(std::shared_ptr<spdlog::logger> logger);

  struct Options {
    int max_messages = 100;
    int keep_recent = 20;
    int max_tokens = 100000;
    int tokens_per_char = 4;
  };

  bool NeedsCompaction(const std::vector<nlohmann::json>& messages,
                       const Options& opts) const;

  using SummaryFn =
      std::function<std::string(const std::vector<nlohmann::json>&)>;

  std::vector<nlohmann::json>
  Compact(const std::vector<nlohmann::json>& messages, const Options& opts,
          SummaryFn summary_fn);

  std::vector<nlohmann::json>
  Truncate(const std::vector<nlohmann::json>& messages, const Options& opts);

  int EstimateTokens(const std::vector<nlohmann::json>& messages) const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw