// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.mmr_reranker;

import std;

export namespace quantclaw {

struct RankedItem {
  std::string id;
  std::string content;
  std::string source;
  int line_number = 0;
  double score = 0.0;
};

// Pre-tokenized view of a RankedItem for efficient similarity computation
struct TokenizedItem {
  std::size_t index;  // index into original items vector
  std::unordered_set<std::string> tokens;
};

class MMRReranker {
 public:
  static std::vector<RankedItem> Rerank(const std::vector<RankedItem>& items,
                                        int top_k, double lambda = 0.7);
  static double JaccardSimilarity(const std::string& a,
                                  const std::string& b);
  // Jaccard on pre-tokenized sets (avoids re-tokenization)
  static double JaccardSimilarity(
      const std::unordered_set<std::string>& set_a,
      const std::unordered_set<std::string>& set_b);
};

}  // namespace quantclaw