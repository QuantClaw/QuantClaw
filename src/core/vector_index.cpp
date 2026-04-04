// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/vector_index.hpp"

#include <algorithm>
#include <cmath>

namespace quantclaw {

void VectorIndex::Add(VectorEntry entry) {
  entries_.push_back(std::move(entry));
}

float VectorIndex::CosineSimilarity(const std::vector<float>& a,
                                    const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty())
    return 0.0f;

  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  if (denom < 1e-10f)
    return 0.0f;
  return dot / denom;
}

std::vector<VectorSearchResult>
VectorIndex::Search(const std::vector<float>& query, int top_k) const {
  if (top_k <= 0)
    return {};

  std::vector<VectorSearchResult> results;
  results.reserve(entries_.size());

  for (const auto& entry : entries_) {
    // Skip entries with mismatched dimensions to avoid false 0.0 scores
    if (entry.embedding.size() != query.size())
      continue;
    float sim = CosineSimilarity(query, entry.embedding);
    results.push_back(
        {entry.id, entry.content, entry.source, entry.line_number, sim});
  }

  // Sort by similarity descending
  std::sort(results.begin(), results.end(),
            [](const VectorSearchResult& a, const VectorSearchResult& b) {
              return a.score > b.score;
            });

  const auto keep_count = static_cast<size_t>(std::max(top_k, 0));
  if (results.size() > keep_count) {
    results.resize(keep_count);
  }
  return results;
}

}  // namespace quantclaw
