// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/context_pruner.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace quantclaw {

int ContextPruner::EstimateTokens(const Message& msg) {
  int chars = static_cast<int>(msg.role.size());
  for (const auto& block : msg.content) {
    chars += static_cast<int>(block.text.size());
    chars += static_cast<int>(block.content.size());
    if (!block.input.is_null())
      chars += static_cast<int>(block.input.dump().size());
  }
  return chars / 4;
}

int ContextPruner::EstimateTokens(const std::vector<Message>& msgs) {
  int total = 0;
  for (const auto& msg : msgs)
    total += EstimateTokens(msg);
  return total;
}

std::vector<Message> ContextPruner::Prune(const std::vector<Message>& history,
                                          const Options& opts) {
  if (history.empty())
    return history;

  // Budget-based pruning: dynamically adjust thresholds based on context window
  Options effective = opts;
  if (opts.context_window > 0) {
    int budget =
        static_cast<int>(opts.context_window * opts.prune_target_ratio) -
        opts.max_tokens;
    if (budget > 0) {
      int current_tokens = EstimateTokens(history);
      if (current_tokens > budget) {
        // Over budget: aggressively prune
        // Scale down max_tool_result_chars and protect_recent
        double over_ratio = static_cast<double>(current_tokens) / budget;
        effective.max_tool_result_chars = std::max(
            200, static_cast<int>(opts.max_tool_result_chars / over_ratio));
        effective.protect_recent =
            std::max(1, static_cast<int>(opts.protect_recent / over_ratio));
        effective.hard_prune_after =
            std::max(3, static_cast<int>(opts.hard_prune_after / over_ratio));
      }
    }
  }

  // Bootstrap protection: find the index of the first user message.
  // Never prune anything at or before this index (system prompts,
  // initial context injection, first user turn).
  int bootstrap_end = -1;
  for (size_t i = 0; i < history.size(); ++i) {
    if (history[i].role == "user") {
      bootstrap_end = static_cast<int>(i);
      break;
    }
  }

  // Find indices of all assistant messages (to determine recency)
  std::vector<int> assistant_indices;
  for (size_t i = 0; i < history.size(); ++i) {
    if (history[i].role == "assistant") {
      assistant_indices.push_back(static_cast<int>(i));
    }
  }

  // Determine which assistant message indices are "protected"
  // (the most recent N assistant messages)
  int num_assistants = static_cast<int>(assistant_indices.size());
  const int protected_assistant_count = std::max(1, effective.protect_recent);
  const int hard_prune_assistant_count =
      std::max(1, effective.hard_prune_after);
  int protect_threshold = -1;  // Messages at or after this index are protected
  if (num_assistants > protected_assistant_count &&
      !assistant_indices.empty()) {
    protect_threshold = assistant_indices[static_cast<size_t>(
        num_assistants - protected_assistant_count)];
  }

  // Hard prune threshold: messages before this many assistant msgs ago
  int hard_threshold = -1;
  if (num_assistants > hard_prune_assistant_count &&
      !assistant_indices.empty()) {
    hard_threshold = assistant_indices[static_cast<size_t>(
        num_assistants - hard_prune_assistant_count)];
  }

  // Build pruned copy
  std::vector<Message> result;
  result.reserve(history.size());

  for (size_t i = 0; i < history.size(); ++i) {
    const auto& msg = history[i];
    const auto message_index = static_cast<int>(i);

    // Check if this message contains tool_result blocks
    bool has_tool_result = false;
    for (const auto& block : msg.content) {
      if (block.type == "tool_result") {
        has_tool_result = true;
        break;
      }
    }

    if (!has_tool_result || protect_threshold < 0 ||
        message_index >= protect_threshold || message_index <= bootstrap_end) {
      // No tool results to prune, within protected range,
      // or within bootstrap region (before first user message)
      result.push_back(msg);
      continue;
    }

    // This message has tool results and is outside the protected range
    Message pruned_msg;
    pruned_msg.role = msg.role;

    for (const auto& block : msg.content) {
      if (block.type != "tool_result") {
        pruned_msg.content.push_back(block);
        continue;
      }

      // Decide soft vs hard prune
      if (hard_threshold >= 0 && message_index < hard_threshold) {
        // Hard prune: replace with placeholder
        pruned_msg.content.push_back(ContentBlock::MakeToolResult(
            block.tool_use_id, "[Tool result omitted — older context]"));
      } else if (static_cast<int>(block.content.size()) >
                 effective.max_tool_result_chars) {
        // Soft prune: truncate long results
        pruned_msg.content.push_back(ContentBlock::MakeToolResult(
            block.tool_use_id,
            soft_prune(block.content, effective.soft_prune_lines)));
      } else {
        // Below size threshold, keep as-is
        pruned_msg.content.push_back(block);
      }
    }

    result.push_back(std::move(pruned_msg));
  }

  return result;
}

std::string ContextPruner::soft_prune(const std::string& content,
                                      int keep_lines) {
  // Split into lines
  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }

  const auto keep_count = static_cast<size_t>(std::max(keep_lines, 0));
  if (keep_count == 0 || lines.size() <= keep_count * 2) {
    return content;  // Short enough, no pruning needed
  }

  // Keep first N and last N lines
  std::string result;
  for (size_t i = 0; i < keep_count; ++i) {
    result += lines[i] + "\n";
  }
  const auto omitted = lines.size() - keep_count * 2;
  result += "\n... [" + std::to_string(omitted) + " lines omitted] ...\n\n";
  for (size_t i = lines.size() - keep_count; i < lines.size(); ++i) {
    result += lines[i];
    if (i + 1 < lines.size())
      result += "\n";
  }

  return result;
}

}  // namespace quantclaw
