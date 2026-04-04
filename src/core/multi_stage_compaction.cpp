// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/multi_stage_compaction.hpp"

#include <algorithm>
#include <cmath>

#include "quantclaw/core/context_pruner.hpp"

namespace quantclaw {

MultiStageCompaction::MultiStageCompaction(
    std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

int MultiStageCompaction::EstimateTokens(const std::vector<Message>& messages) {
  return ContextPruner::EstimateTokens(messages);
}

std::vector<std::vector<Message>>
MultiStageCompaction::SplitByTokenShare(const std::vector<Message>& messages,
                                        int parts) {
  if (parts <= 1 || messages.empty()) {
    return {messages};
  }

  int total_tokens = EstimateTokens(messages);
  int target_per_part = std::max(1, total_tokens / parts);

  std::vector<std::vector<Message>> result;
  std::vector<Message> current_chunk;
  int current_tokens = 0;

  for (const auto& msg : messages) {
    int msg_tokens = ContextPruner::EstimateTokens(msg);
    current_chunk.push_back(msg);
    current_tokens += msg_tokens;

    // Start a new chunk when we've accumulated enough tokens
    // (but don't create more chunks than requested)
    if (current_tokens >= target_per_part &&
        static_cast<int>(result.size()) < parts - 1) {
      result.push_back(std::move(current_chunk));
      current_chunk.clear();
      current_tokens = 0;
    }
  }

  // Add the remaining messages as the last chunk
  if (!current_chunk.empty()) {
    result.push_back(std::move(current_chunk));
  }

  return result;
}

std::vector<std::vector<Message>>
MultiStageCompaction::ChunkByMaxTokens(const std::vector<Message>& messages,
                                       int max_tokens) {
  if (messages.empty()) {
    return {};
  }
  if (max_tokens <= 0) {
    return {messages};
  }

  std::vector<std::vector<Message>> result;
  std::vector<Message> current_chunk;
  int current_tokens = 0;

  for (const auto& msg : messages) {
    int msg_tokens = ContextPruner::EstimateTokens(msg);

    // If adding this message exceeds the limit, start a new chunk
    // (but always add at least one message per chunk)
    if (!current_chunk.empty() && current_tokens + msg_tokens > max_tokens) {
      result.push_back(std::move(current_chunk));
      current_chunk.clear();
      current_tokens = 0;
    }

    current_chunk.push_back(msg);
    current_tokens += msg_tokens;
  }

  if (!current_chunk.empty()) {
    result.push_back(std::move(current_chunk));
  }

  return result;
}

std::vector<Message>
MultiStageCompaction::CompactMultiStage(const std::vector<Message>& messages,
                                        const CompactionOptions& opts,
                                        SummaryFn summary_fn) {
  if (!summary_fn) {
    logger_->warn("No summary function provided, returning messages as-is");
    return messages;
  }

  int total_tokens = EstimateTokens(messages);
  int msg_count = static_cast<int>(messages.size());

  // Small enough for single-pass
  if (msg_count < opts.min_messages_for_multistage ||
      total_tokens <= opts.max_chunk_tokens) {
    logger_->info("Single-pass compaction: {} messages, ~{} tokens", msg_count,
                  total_tokens);

    std::string summary = summary_fn(messages);
    std::vector<Message> result;
    result.push_back(
        Message{"system", "[Compacted context (" + std::to_string(msg_count) +
                              " messages, ~" + std::to_string(total_tokens) +
                              " tokens)]\n\n" + summary});
    return result;
  }

  // Multi-stage: chunk → summarize each → merge
  auto chunks = ChunkByMaxTokens(messages, opts.max_chunk_tokens);
  int num_chunks = static_cast<int>(chunks.size());

  logger_->info(
      "Multi-stage compaction: {} messages split into {} chunks "
      "(~{} tokens total)",
      msg_count, num_chunks, total_tokens);

  // Stage 1: Summarize each chunk
  std::vector<std::string> chunk_summaries;
  chunk_summaries.reserve(chunks.size());

  for (size_t i = 0; i < chunks.size(); ++i) {
    int chunk_tokens = EstimateTokens(chunks[i]);
    logger_->debug("Summarizing chunk {}/{}: {} messages, ~{} tokens", i + 1,
                   num_chunks, static_cast<int>(chunks[i].size()),
                   chunk_tokens);

    std::string summary = summary_fn(chunks[i]);
    chunk_summaries.push_back(std::move(summary));
  }

  // Stage 2: Merge chunk summaries
  // If merged summaries are small enough, combine directly.
  // Otherwise, recursively compact.
  std::string merged;
  for (size_t i = 0; i < chunks.size(); ++i) {
    if (!merged.empty()) {
      merged += "\n\n---\n\n";
    }
    merged += "[Part " + std::to_string(i + 1) + "/" +
              std::to_string(num_chunks) + "]\n" + chunk_summaries[i];
  }

  int merged_tokens = static_cast<int>(merged.size()) / 4;  // rough estimate

  // If merged result exceeds target and we have a target, do a final pass
  if (opts.target_tokens > 0 &&
      merged_tokens >
          static_cast<int>(opts.target_tokens * opts.safety_margin)) {
    logger_->info(
        "Merged summaries (~{} tokens) exceed target ({}), "
        "running final summarization pass",
        merged_tokens, opts.target_tokens);

    std::vector<Message> merge_input;
    merge_input.push_back(Message{"system", merged});
    std::string final_summary = summary_fn(merge_input);
    merged = final_summary;
  }

  std::vector<Message> result;
  result.push_back(
      Message{"system", "[Compacted context (" + std::to_string(msg_count) +
                            " messages, ~" + std::to_string(total_tokens) +
                            " tokens, " + std::to_string(num_chunks) +
                            " stages)]\n\n" + merged});

  logger_->info("Multi-stage compaction complete: {} → 1 message", msg_count);

  return result;
}

}  // namespace quantclaw
