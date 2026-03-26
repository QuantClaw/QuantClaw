// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.vector_index;

import std;

export namespace quantclaw {

struct VectorEntry {
  std::string id;
  std::vector<float> embedding;
  std::string content;
  std::string source;
  int line_number = 0;
};

struct VectorSearchResult {
  std::string id;
  std::string content;
  std::string source;
  int line_number;
  float score;
};

class VectorIndex {
 public:
  void Add(VectorEntry entry);
  std::vector<VectorSearchResult> Search(const std::vector<float>& query,
                                         int top_k = 10) const;
  std::size_t Size() const {
    return entries_.size();
  }
  void Clear() {
    entries_.clear();
  }
  static float CosineSimilarity(const std::vector<float>& a,
                                const std::vector<float>& b);

 private:
  std::vector<VectorEntry> entries_;
};

}  // namespace quantclaw