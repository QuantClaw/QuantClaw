// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module quantclaw.core.mmr_reranker;

import std;

namespace quantclaw {

namespace {

std::unordered_set<std::string> tokenize_to_set(std::string_view text) {
  std::unordered_set<std::string> tokens;
  std::size_t start = std::string_view::npos;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    bool alnum = i < text.size() &&
                 std::isalnum(static_cast<unsigned char>(text[i]));
    if (alnum) {
      if (start == std::string_view::npos)
        start = i;
    } else if (start != std::string_view::npos) {
      // Construct token in-place: lowercase the slice without a temporary string
      std::string token(text.substr(start, i - start));
      for (char& c : token)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      tokens.insert(std::move(token));
      start = std::string_view::npos;
    }
  }
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

  for (int k = 0; k < top_k && k < static_cast<int>(items.size()); ++k) {
    double best_mmr = -1e9;
    int best_idx = -1;

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
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

    if (best_idx >= 0) {
      picked[best_idx] = true;
      selected.push_back(items[best_idx]);
    }
  }

  return selected;
}

}  // namespace quantclaw
