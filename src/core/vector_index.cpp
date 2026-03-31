// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module quantclaw.core.vector_index;

import std;

namespace quantclaw {

void VectorIndex::Add(VectorEntry entry) {
  entries_.push_back(std::move(entry));
}

float VectorIndex::CosineSimilarity(const std::vector<float>& a,
                                    const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty())
    return 0.0f;

  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
  for (std::size_t i = 0; i < a.size(); ++i) {
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

  // Partial sort: only order the top_k elements, reducing O(N log N) to O(N log K)
  auto cmp = [](const VectorSearchResult& a, const VectorSearchResult& b) {
    return a.score > b.score;
  };
  int k = std::min(top_k, static_cast<int>(results.size()));
  std::partial_sort(results.begin(), results.begin() + k, results.end(), cmp);
  results.resize(k);
  return results;
}

}  // namespace quantclaw
