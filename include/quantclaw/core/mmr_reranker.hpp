// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace quantclaw {

struct RankedItem {
  std::string id;
  std::string content;
  std::string source;
  int line_number = 0;
  double score = 0.0;  // Relevance score from retrieval
};

// Maximal Marginal Relevance (MMR) re-ranker.
//
// Balances relevance and diversity: selects items that are both relevant
// to the query and diverse from each other. Uses Jaccard similarity
// between token sets to measure inter-document similarity.
//
// MMR(d) = λ * Relevance(d) − (1−λ) * max_{selected} Similarity(d, selected)
class MMRReranker {
 public:
  // Re-rank items using MMR.
  //   items: candidate items with relevance scores
  //   top_k: number of items to select
  //   lambda: trade-off between relevance (1.0) and diversity (0.0)
  static std::vector<RankedItem> Rerank(const std::vector<RankedItem>& items,
                                        int top_k, double lambda = 0.7);

  // Compute Jaccard similarity between two texts (token overlap).
  static double JaccardSimilarity(const std::string& a, const std::string& b);
};

}  // namespace quantclaw
