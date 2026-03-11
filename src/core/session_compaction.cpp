// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/session_compaction.hpp"

namespace quantclaw {

SessionCompaction::SessionCompaction(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

bool SessionCompaction::NeedsCompaction(
    const std::vector<nlohmann::json>& messages, const Options& opts) const {
  if (static_cast<int>(messages.size()) > opts.max_messages)
    return true;
  if (EstimateTokens(messages) > opts.max_tokens)
    return true;
  return false;
}

std::vector<nlohmann::json>
SessionCompaction::Compact(const std::vector<nlohmann::json>& messages,
                           const Options& opts, SummaryFn summary_fn) {
  if (!NeedsCompaction(messages, opts)) {
    return messages;
  }

  int total = static_cast<int>(messages.size());
  int keep = std::min(opts.keep_recent, total);
  int to_summarize = total - keep;

  if (to_summarize <= 0) {
    return messages;
  }

  // Split into old (to summarize) and recent (to keep)
  std::vector<nlohmann::json> old_msgs(messages.begin(),
                                       messages.begin() + to_summarize);
  std::vector<nlohmann::json> recent(messages.begin() + to_summarize,
                                     messages.end());

  // Generate summary of old messages
  std::string summary;
  if (summary_fn) {
    try {
      summary = summary_fn(old_msgs);
    } catch (const std::exception& e) {
      logger_->error("Compaction summary failed: {}", e.what());
      // Fallback to truncation
      return Truncate(messages, opts);
    }
  }

  if (summary.empty()) {
    return Truncate(messages, opts);
  }

  // Build compacted message list: system summary + recent messages
  std::vector<nlohmann::json> result;
  result.push_back({
      {"role", "system"},
      {"content", "[Compacted conversation summary]\n" + summary},
      {"metadata", {{"compacted", true}, {"summarized_count", to_summarize}}},
  });

  for (auto& msg : recent) {
    result.push_back(std::move(msg));
  }

  logger_->info("Compacted {} messages into summary + {} recent", to_summarize,
                keep);
  return result;
}

std::vector<nlohmann::json>
SessionCompaction::Truncate(const std::vector<nlohmann::json>& messages,
                            const Options& opts) {
  int total = static_cast<int>(messages.size());
  int keep = std::min(opts.keep_recent, total);

  if (total <= keep)
    return messages;

  std::vector<nlohmann::json> result(messages.end() - keep, messages.end());

  // Prepend a system note about truncation
  nlohmann::json note;
  note["role"] = "system";
  note["content"] = "[Earlier messages truncated. " +
                    std::to_string(total - keep) + " messages removed.]";
  note["metadata"] = {{"truncated", true}, {"removed_count", total - keep}};
  result.insert(result.begin(), note);

  logger_->info("Truncated {} messages, keeping {} recent", total - keep, keep);
  return result;
}

int SessionCompaction::EstimateTokens(
    const std::vector<nlohmann::json>& messages) const {
  int total_chars = 0;
  for (const auto& msg : messages) {
    if (msg.contains("content") && msg["content"].is_string()) {
      total_chars += static_cast<int>(msg["content"].get<std::string>().size());
    }
  }
  // Rough estimate: ~4 chars per token for English
  return total_chars / 4;
}

}  // namespace quantclaw
