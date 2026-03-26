// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include "quantclaw/providers/llm_provider.hpp"

export module quantclaw.core.multi_stage_compaction;

import std;

namespace spdlog {
class logger;
}
import quantclaw.core.context_pruner;

export namespace quantclaw {

using SummaryFn = std::function<std::string(const std::vector<Message>&)>;

struct CompactionOptions {
  int target_tokens = 0;
  int max_chunk_tokens = 16384;
  double safety_margin = 1.2;
  int min_messages_for_multistage = 8;
};

class MultiStageCompaction {
 public:
  explicit MultiStageCompaction(std::shared_ptr<spdlog::logger> logger);

  static std::vector<std::vector<Message>> SplitByTokenShare(
      const std::vector<Message>& messages, int parts);
  static std::vector<std::vector<Message>> ChunkByMaxTokens(
      const std::vector<Message>& messages, int max_tokens);
  static int EstimateTokens(const std::vector<Message>& messages);

  std::vector<Message> CompactMultiStage(const std::vector<Message>& messages,
                                         const CompactionOptions& opts,
                                         SummaryFn summary_fn);

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw