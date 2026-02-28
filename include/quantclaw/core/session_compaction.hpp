#pragma once

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// Session compaction: when message history grows too large, summarize older
// messages to keep the context window manageable.
class SessionCompaction {
 public:
  explicit SessionCompaction(std::shared_ptr<spdlog::logger> logger);

  struct Options {
    int max_messages = 100;    // Compact when exceeding this count
    int keep_recent = 20;      // Always keep this many recent messages
    int max_tokens = 100000;   // Approximate token limit
    int tokens_per_char = 4;   // Rough chars-per-token estimate
  };

  // Check if compaction is needed
  bool needs_compaction(const std::vector<nlohmann::json>& messages,
                        const Options& opts) const;

  // Compact messages: returns new message list with older messages summarized.
  // The summary_fn callback is called with the messages to summarize and
  // should return a summary string (typically via LLM).
  using SummaryFn = std::function<std::string(const std::vector<nlohmann::json>&)>;

  std::vector<nlohmann::json> compact(
      const std::vector<nlohmann::json>& messages,
      const Options& opts,
      SummaryFn summary_fn);

  // Simple truncation without LLM summary (just keeps recent messages)
  std::vector<nlohmann::json> truncate(
      const std::vector<nlohmann::json>& messages,
      const Options& opts);

  // Estimate token count for a message list
  int estimate_tokens(const std::vector<nlohmann::json>& messages) const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace quantclaw
