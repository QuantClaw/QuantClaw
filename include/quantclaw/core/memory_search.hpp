// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/core/mmr_reranker.hpp"
#include "quantclaw/core/temporal_decay.hpp"
#include "quantclaw/core/vector_index.hpp"
#include "quantclaw/providers/embedding_provider.hpp"

namespace quantclaw {

struct MemorySearchResult {
  std::string source;   // file path
  std::string content;  // matching line/paragraph
  double score;         // relevance score (0-1)
  int line_number;      // line number in source
};

// Hybrid memory search options.
struct HybridSearchOptions {
  double bm25_weight = 0.5;    // Weight for BM25 scores
  double vector_weight = 0.5;  // Weight for vector similarity scores
  bool use_temporal_decay = true;
  bool use_mmr = true;
  double mmr_lambda = 0.7;  // MMR relevance/diversity trade-off
  int max_results = 10;
};

// Full-text memory search across workspace memory files.
// Supports keyword matching with BM25 scoring (Okapi BM25).
// Optionally supports hybrid search: BM25 + vector similarity + temporal
// decay + MMR re-ranking.
class MemorySearch {
 public:
  explicit MemorySearch(std::shared_ptr<spdlog::logger> logger);

  // Index memory files from a workspace directory
  void IndexDirectory(const std::filesystem::path& dir);

  // Add a single file to the index
  void IndexFile(const std::filesystem::path& file);

  // Search for relevant memory entries (BM25 only)
  std::vector<MemorySearchResult> Search(const std::string& query,
                                         int max_results = 10) const;

  // Hybrid search: BM25 + vector + temporal decay + MMR.
  // Falls back to BM25-only if no embedding provider is set.
  std::vector<MemorySearchResult>
  HybridSearch(const std::string& query,
               const HybridSearchOptions& opts = {}) const;

  // Set embedding provider for vector search
  void SetEmbeddingProvider(std::shared_ptr<EmbeddingProvider> provider);

  // Build vector index for all indexed entries (requires embedding provider)
  void BuildVectorIndex();

  // Get index stats
  nlohmann::json Stats() const;

  // Clear the index
  void Clear();

 private:
  struct IndexEntry {
    std::string filepath;
    int line_number;
    std::string content;
    std::vector<std::string> tokens;
  };

  // Tokenize text into lowercase words
  static std::vector<std::string> tokenize(const std::string& text);

  // Score a document against query tokens using BM25
  double score_entry(const IndexEntry& entry,
                     const std::vector<std::string>& query_tokens) const;

  // Compute document frequency for a term (number of entries containing it)
  int document_frequency(const std::string& term) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::vector<IndexEntry> entries_;
  int total_documents_ = 0;
  double avg_doc_length_ = 0;  // Average document length for BM25

  // BM25 parameters
  static constexpr double kBM25_k1 = 1.2;
  static constexpr double kBM25_b = 0.75;

  // Hybrid search components
  std::shared_ptr<EmbeddingProvider> embedding_provider_;
  VectorIndex vector_index_;
  TemporalDecay temporal_decay_;
};

}  // namespace quantclaw
