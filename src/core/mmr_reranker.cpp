// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/mmr_reranker.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace quantclaw {

namespace {

std::unordered_set<std::string> tokenize_to_set(const std::string& text) {
  std::unordered_set<std::string> tokens;
  std::string word;
  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!word.empty()) {
      tokens.insert(word);
      word.clear();
    }
  }
  if (!word.empty())
    tokens.insert(word);
  return tokens;
}

}  // namespace

double MMRReranker::JaccardSimilarity(const std::string& a,
                                      const std::string& b) {
  auto set_a = tokenize_to_set(a);
  auto set_b = tokenize_to_set(b);

  if (set_a.empty() && set_b.empty())
    return 1.0;
  if (set_a.empty() || set_b.empty())
    return 0.0;

  int intersection = 0;
  for (const auto& token : set_a) {
    if (set_b.count(token))
      intersection++;
  }

  int union_size = static_cast<int>(set_a.size() + set_b.size()) - intersection;
  if (union_size == 0)
    return 0.0;
  return static_cast<double>(intersection) / union_size;
}

std::vector<RankedItem>
MMRReranker::Rerank(const std::vector<RankedItem>& items, int top_k,
                    double lambda) {
  if (items.empty() || top_k <= 0)
    return {};
  if (static_cast<int>(items.size()) <= top_k)
    return items;

  std::vector<RankedItem> selected;
  std::vector<bool> picked(items.size(), false);

  const auto keep_count = std::min(items.size(), static_cast<size_t>(top_k));
  for (size_t k = 0; k < keep_count; ++k) {
    double best_mmr = -1e9;
    size_t best_idx = items.size();

    for (size_t i = 0; i < items.size(); ++i) {
      if (picked[i])
        continue;

      // Compute max similarity to already selected items
      double max_sim = 0.0;
      for (const auto& sel : selected) {
        double sim = JaccardSimilarity(items[i].content, sel.content);
        if (sim > max_sim)
          max_sim = sim;
      }

      // MMR score
      double mmr = lambda * items[i].score - (1.0 - lambda) * max_sim;

      if (mmr > best_mmr) {
        best_mmr = mmr;
        best_idx = i;
      }
    }

    if (best_idx < items.size()) {
      picked[best_idx] = true;
      selected.push_back(items[best_idx]);
    }
  }

  return selected;
}

}  // namespace quantclaw
