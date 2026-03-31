// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.memory_search;

import std;
import nlohmann.json;
import quantclaw.providers.embedding_provider;
import quantclaw.core.mmr_reranker;
import quantclaw.core.temporal_decay;
import quantclaw.core.vector_index;

export namespace quantclaw {

struct MemorySearchResult {
  std::string source;
  std::string content;
  double score;
  int line_number;
};

struct HybridSearchOptions {
  double bm25_weight = 0.5;
  double vector_weight = 0.5;
  bool use_temporal_decay = true;
  bool use_mmr = true;
  double mmr_lambda = 0.7;
  int max_results = 10;
};

class MemorySearch {
 public:
  explicit MemorySearch(std::shared_ptr<spdlog::logger> logger);

  void IndexDirectory(const std::filesystem::path& dir);
  void IndexFile(const std::filesystem::path& file);
  std::vector<MemorySearchResult> Search(const std::string& query,
                                         int max_results = 10) const;
  std::vector<MemorySearchResult>
  HybridSearch(const std::string& query,
               const HybridSearchOptions& opts = {}) const;
  void SetEmbeddingProvider(std::shared_ptr<EmbeddingProvider> provider);
  void BuildVectorIndex();
  nlohmann::json Stats() const;
  void Clear();

 private:
  struct IndexEntry {
    std::string filepath;
    int line_number;
    std::string content;
    std::vector<std::string> tokens;
  };

  static std::vector<std::string> tokenize(const std::string& text);
  double score_entry(const IndexEntry& entry,
                     const std::vector<std::string>& query_tokens) const;
  int document_frequency(const std::string& term) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::vector<IndexEntry> entries_;
  int total_documents_ = 0;
  double avg_doc_length_ = 0;
  static constexpr double kBM25_k1 = 1.2;
  static constexpr double kBM25_b = 0.75;
  std::shared_ptr<EmbeddingProvider> embedding_provider_;
  VectorIndex vector_index_;
  TemporalDecay temporal_decay_;
};

}  // namespace quantclaw