// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.context_pruner;

import std;

#include "quantclaw/providers/llm_provider.hpp"

export namespace quantclaw {

class ContextPruner {
 public:
  struct Options {
    int protect_recent = 3;
    int soft_prune_lines = 5;
    int hard_prune_after = 10;
    int max_tool_result_chars = 2000;
    int context_window = 0;
    int max_tokens = 8192;
    double prune_target_ratio = 0.75;
  };

  static std::vector<Message> Prune(const std::vector<Message>& history,
                                    const Options& opts);
  static int EstimateTokens(const Message& msg);
  static int EstimateTokens(const std::vector<Message>& msgs);

 private:
  static std::string soft_prune(const std::string& content, int keep_lines);
};

}  // namespace quantclaw