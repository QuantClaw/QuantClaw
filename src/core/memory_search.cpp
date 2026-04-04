// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/memory_search.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace quantclaw {

static size_t clamp_result_count(std::shared_ptr<spdlog::logger> logger,
                                 int max_results) {
  if (max_results < 0 && logger) {
    logger->warn("Negative max_results={} treated as 0", max_results);
  }
  return static_cast<size_t>(std::max(max_results, 0));
}

MemorySearch::MemorySearch(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void MemorySearch::IndexDirectory(const std::filesystem::path& dir) {
  if (!std::filesystem::exists(dir))
    return;

  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (!entry.is_regular_file())
      continue;
    auto ext = entry.path().extension().string();
    if (ext == ".md" || ext == ".txt" || ext == ".jsonl") {
      IndexFile(entry.path());
    }
  }

  // Recompute average document length for BM25
  if (!entries_.empty()) {
    double total_len = 0;
    for (const auto& e : entries_) {
      total_len += static_cast<double>(e.tokens.size());
    }
    avg_doc_length_ = total_len / static_cast<double>(entries_.size());
  }

  logger_->info("Indexed {} entries from {}", entries_.size(), dir.string());
}

void MemorySearch::IndexFile(const std::filesystem::path& file) {
  std::ifstream ifs(file);
  if (!ifs.is_open())
    return;

  std::string line;
  int line_num = 0;
  std::string paragraph;
  int para_start = 1;

  auto flush_paragraph = [&]() {
    if (paragraph.empty())
      return;
    IndexEntry entry;
    entry.filepath = file.string();
    entry.line_number = para_start;
    entry.content = paragraph;
    entry.tokens = tokenize(paragraph);
    if (!entry.tokens.empty()) {
      entries_.push_back(std::move(entry));
      total_documents_++;
    }
    paragraph.clear();
  };

  while (std::getline(ifs, line)) {
    line_num++;

    // Split on empty lines (paragraph boundaries)
    if (line.empty() ||
        line.find_first_not_of(" \t\r\n") == std::string::npos) {
      flush_paragraph();
      para_start = line_num + 1;
      continue;
    }

    if (paragraph.empty()) {
      para_start = line_num;
    }
    if (!paragraph.empty())
      paragraph += "\n";
    paragraph += line;
  }
  flush_paragraph();

  // Update average document length for BM25
  if (!entries_.empty()) {
    double total_len = 0;
    for (const auto& e : entries_) {
      total_len += static_cast<double>(e.tokens.size());
    }
    avg_doc_length_ = total_len / static_cast<double>(entries_.size());
  }
}

std::vector<MemorySearchResult> MemorySearch::Search(const std::string& query,
                                                     int max_results) const {
  auto query_tokens = tokenize(query);
  if (query_tokens.empty())
    return {};

  std::vector<std::pair<double, const IndexEntry*>> scored;
  for (const auto& entry : entries_) {
    double s = score_entry(entry, query_tokens);
    if (s > 0) {
      scored.push_back({s, &entry});
    }
  }

  // Sort by score descending
  std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<MemorySearchResult> results;
  const auto count =
      std::min(scored.size(), clamp_result_count(logger_, max_results));
  for (size_t i = 0; i < count; ++i) {
    MemorySearchResult r;
    r.source = scored[i].second->filepath;
    r.content = scored[i].second->content;
    r.score = scored[i].first;
    r.line_number = scored[i].second->line_number;
    results.push_back(std::move(r));
  }
  return results;
}

nlohmann::json MemorySearch::Stats() const {
  return {
      {"indexed_entries", entries_.size()},
      {"total_documents", total_documents_},
  };
}

void MemorySearch::SetEmbeddingProvider(
    std::shared_ptr<EmbeddingProvider> provider) {
  embedding_provider_ = std::move(provider);
}

void MemorySearch::BuildVectorIndex() {
  if (!embedding_provider_) {
    logger_->warn("No embedding provider set, cannot build vector index");
    return;
  }

  vector_index_.Clear();

  // Batch embed all entries
  std::vector<std::string> texts;
  texts.reserve(entries_.size());
  for (const auto& entry : entries_) {
    texts.push_back(entry.content);
  }

  if (texts.empty())
    return;

  EmbeddingRequest req;
  req.texts = texts;
  auto resp = embedding_provider_->Embed(req);

  if (resp.embeddings.size() != entries_.size()) {
    logger_->error("Embedding count mismatch: got {} for {} entries",
                   resp.embeddings.size(), entries_.size());
    return;
  }

  for (size_t i = 0; i < entries_.size(); ++i) {
    VectorEntry ve;
    ve.id =
        entries_[i].filepath + ":" + std::to_string(entries_[i].line_number);
    ve.embedding = std::move(resp.embeddings[i]);
    ve.content = entries_[i].content;
    ve.source = entries_[i].filepath;
    ve.line_number = entries_[i].line_number;
    vector_index_.Add(std::move(ve));
  }

  logger_->info("Built vector index with {} entries", vector_index_.Size());
}

std::vector<MemorySearchResult>
MemorySearch::HybridSearch(const std::string& query,
                           const HybridSearchOptions& opts) const {
  // Get BM25 results
  auto bm25_results = Search(query, opts.max_results * 3);

  // If no embedding provider or empty vector index, fall back to BM25-only
  if (!embedding_provider_ || vector_index_.Size() == 0) {
    // Apply temporal decay and MMR even without vector search
    if (!opts.use_temporal_decay && !opts.use_mmr) {
      bm25_results.resize(std::min(
          bm25_results.size(), clamp_result_count(logger_, opts.max_results)));
      return bm25_results;
    }

    // Apply temporal decay
    if (opts.use_temporal_decay) {
      for (auto& r : bm25_results) {
        double decay = temporal_decay_.Score(std::filesystem::path(r.source));
        r.score *= decay;
      }
      std::sort(bm25_results.begin(), bm25_results.end(),
                [](const auto& a, const auto& b) { return a.score > b.score; });
    }

    // Apply MMR
    if (opts.use_mmr &&
        static_cast<int>(bm25_results.size()) > opts.max_results) {
      std::vector<RankedItem> items;
      for (const auto& r : bm25_results) {
        items.push_back({r.source + ":" + std::to_string(r.line_number),
                         r.content, r.source, r.line_number, r.score});
      }
      auto reranked =
          MMRReranker::Rerank(items, opts.max_results, opts.mmr_lambda);
      std::vector<MemorySearchResult> final_results;
      for (const auto& item : reranked) {
        final_results.push_back(
            {item.source, item.content, item.score, item.line_number});
      }
      return final_results;
    }

    bm25_results.resize(std::min(
        bm25_results.size(), clamp_result_count(logger_, opts.max_results)));
    return bm25_results;
  }

  // Embed query
  EmbeddingRequest req;
  req.texts = {query};
  auto resp = embedding_provider_->Embed(req);
  if (resp.embeddings.empty()) {
    // Embedding failed, fall back to BM25
    bm25_results.resize(std::min(
        bm25_results.size(), clamp_result_count(logger_, opts.max_results)));
    return bm25_results;
  }

  // Get vector search results
  auto vec_results =
      vector_index_.Search(resp.embeddings[0], opts.max_results * 3);

  // Merge: normalize and combine scores
  // Build a map of id -> combined score
  struct MergedEntry {
    std::string source;
    std::string content;
    int line_number;
    double bm25_score = 0;
    double vec_score = 0;
    double combined = 0;
  };

  std::unordered_map<std::string, MergedEntry> merged;

  // Normalize BM25 scores to [0,1]
  double max_bm25 = 0;
  for (const auto& r : bm25_results) {
    if (r.score > max_bm25)
      max_bm25 = r.score;
  }
  for (const auto& r : bm25_results) {
    std::string key = r.source + ":" + std::to_string(r.line_number);
    auto& m = merged[key];
    m.source = r.source;
    m.content = r.content;
    m.line_number = r.line_number;
    m.bm25_score = max_bm25 > 0 ? r.score / max_bm25 : 0;
  }

  // Normalize vector scores (cosine sim already in [-1,1], shift to [0,1])
  for (const auto& r : vec_results) {
    std::string key = r.source + ":" + std::to_string(r.line_number);
    auto& m = merged[key];
    if (m.source.empty()) {
      m.source = r.source;
      m.content = r.content;
      m.line_number = r.line_number;
    }
    m.vec_score = (r.score + 1.0) / 2.0;  // Map [-1,1] to [0,1]
  }

  // Combine scores with weights
  std::vector<MergedEntry> all_entries;
  for (auto& [key, m] : merged) {
    m.combined =
        opts.bm25_weight * m.bm25_score + opts.vector_weight * m.vec_score;

    // Apply temporal decay
    if (opts.use_temporal_decay) {
      double decay = temporal_decay_.Score(std::filesystem::path(m.source));
      m.combined *= decay;
    }

    all_entries.push_back(std::move(m));
  }

  // Sort by combined score
  std::sort(
      all_entries.begin(), all_entries.end(),
      [](const auto& a, const auto& b) { return a.combined > b.combined; });

  // Apply MMR
  if (opts.use_mmr && static_cast<int>(all_entries.size()) > opts.max_results) {
    std::vector<RankedItem> items;
    for (const auto& e : all_entries) {
      items.push_back({e.source + ":" + std::to_string(e.line_number),
                       e.content, e.source, e.line_number, e.combined});
    }
    auto reranked =
        MMRReranker::Rerank(items, opts.max_results, opts.mmr_lambda);
    std::vector<MemorySearchResult> final_results;
    for (const auto& item : reranked) {
      final_results.push_back(
          {item.source, item.content, item.score, item.line_number});
    }
    return final_results;
  }

  // Return top results
  std::vector<MemorySearchResult> results;
  const auto count = std::min(all_entries.size(),
                              clamp_result_count(logger_, opts.max_results));
  for (size_t i = 0; i < count; ++i) {
    results.push_back({all_entries[i].source, all_entries[i].content,
                       all_entries[i].combined, all_entries[i].line_number});
  }
  return results;
}

void MemorySearch::Clear() {
  entries_.clear();
  total_documents_ = 0;
  avg_doc_length_ = 0;
  vector_index_.Clear();
}

std::vector<std::string> MemorySearch::tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::string word;

  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      if (!word.empty() && word.size() > 1) {
        tokens.push_back(word);
      }
      word.clear();
    }
  }
  if (!word.empty() && word.size() > 1) {
    tokens.push_back(word);
  }
  return tokens;
}

int MemorySearch::document_frequency(const std::string& term) const {
  int count = 0;
  for (const auto& entry : entries_) {
    for (const auto& t : entry.tokens) {
      if (t == term) {
        count++;
        break;  // Count each document once
      }
    }
  }
  return count;
}

double
MemorySearch::score_entry(const IndexEntry& entry,
                          const std::vector<std::string>& query_tokens) const {
  if (entry.tokens.empty() || total_documents_ == 0)
    return 0;

  // Build term frequency map for the entry
  std::unordered_map<std::string, int> tf;
  for (const auto& t : entry.tokens) {
    tf[t]++;
  }

  double doc_len = static_cast<double>(entry.tokens.size());
  double avgdl = avg_doc_length_ > 0 ? avg_doc_length_ : doc_len;
  double N = static_cast<double>(total_documents_);

  // BM25 scoring: score(D, Q) = Σ IDF(qi) * (f(qi,D) * (k1+1)) / (f(qi,D) +
  // k1*(1-b+b*|D|/avgDL))
  double score = 0;

  for (const auto& qt : query_tokens) {
    auto it = tf.find(qt);
    if (it == tf.end())
      continue;

    double f = static_cast<double>(it->second);  // term frequency in doc
    int df = document_frequency(qt);             // document frequency

    // IDF component: log((N - df + 0.5) / (df + 0.5) + 1)
    double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);

    // BM25 TF component
    double tf_component =
        (f * (kBM25_k1 + 1.0)) /
        (f + kBM25_k1 * (1.0 - kBM25_b + kBM25_b * doc_len / avgdl));

    score += idf * tf_component;
  }

  return score;
}

}  // namespace quantclaw
