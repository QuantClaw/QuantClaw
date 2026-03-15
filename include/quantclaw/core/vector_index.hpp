// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace quantclaw {

struct VectorEntry {
  std::string id;  // Unique identifier (e.g., "filepath:line")
  std::vector<float> embedding;
  std::string content;  // Original text
  std::string source;   // Source file path
  int line_number = 0;
};

struct VectorSearchResult {
  std::string id;
  std::string content;
  std::string source;
  int line_number;
  float score;  // Cosine similarity [−1, 1]
};

// In-memory vector index with brute-force cosine similarity search.
// Suitable for workspace-scale data (hundreds to low thousands of entries).
class VectorIndex {
 public:
  // Add an entry to the index.
  void Add(VectorEntry entry);

  // Search for the top-k most similar entries to the query embedding.
  std::vector<VectorSearchResult> Search(const std::vector<float>& query,
                                         int top_k = 10) const;

  // Number of entries in the index.
  size_t Size() const {
    return entries_.size();
  }

  // Clear all entries.
  void Clear() {
    entries_.clear();
  }

  // Compute cosine similarity between two vectors.
  static float CosineSimilarity(const std::vector<float>& a,
                                const std::vector<float>& b);

 private:
  std::vector<VectorEntry> entries_;
};

}  // namespace quantclaw
