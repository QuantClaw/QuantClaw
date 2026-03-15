// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/providers/llm_provider.hpp"

namespace quantclaw {

// Summary function: given messages, return a summary string.
using SummaryFn = std::function<std::string(const std::vector<Message>&)>;

// Options for multi-stage compaction.
struct CompactionOptions {
  int target_tokens = 0;         // Target token budget for output
  int max_chunk_tokens = 16384;  // Max tokens per chunk before splitting
  double safety_margin = 1.2;    // Over-estimate factor for token counting
  int min_messages_for_multistage = 8;  // Below this, use single-pass
};

// Multi-stage compaction: chunk-and-merge strategy for large contexts.
//
// When the message history is too large for a single summarization pass:
//   1. Split messages into chunks (by token share or max tokens)
//   2. Summarize each chunk independently
//   3. Merge chunk summaries into a final compacted context
//
// For small histories, degrades gracefully to single-pass summarization.
class MultiStageCompaction {
 public:
  explicit MultiStageCompaction(std::shared_ptr<spdlog::logger> logger);

  // Split messages into N roughly equal parts by token share.
  static std::vector<std::vector<Message>>
  SplitByTokenShare(const std::vector<Message>& messages, int parts);

  // Split messages into chunks where each chunk is at most max_tokens.
  static std::vector<std::vector<Message>>
  ChunkByMaxTokens(const std::vector<Message>& messages, int max_tokens);

  // Estimate token count for a set of messages.
  static int EstimateTokens(const std::vector<Message>& messages);

  // Run multi-stage compaction. Returns compacted messages.
  // If messages are small enough, falls back to single-pass.
  std::vector<Message> CompactMultiStage(const std::vector<Message>& messages,
                                         const CompactionOptions& opts,
                                         SummaryFn summary_fn);

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
