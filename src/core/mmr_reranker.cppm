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

class MMRReranker {
 public:
  static std::vector<RankedItem> Rerank(const std::vector<RankedItem>& items,
                                        int top_k, double lambda = 0.7);
  static double JaccardSimilarity(const std::string& a,
                                  const std::string& b);
};

}  // namespace quantclaw