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

MemorySearch::MemorySearch(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void MemorySearch::IndexDirectory(const std::filesystem::path& dir) {
  if (!std::filesystem::exists(dir)) return;

  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension().string();
    if (ext == ".md" || ext == ".txt" || ext == ".jsonl") {
      IndexFile(entry.path());
    }
  }

  logger_->info("Indexed {} entries from {}", entries_.size(), dir.string());
}

void MemorySearch::IndexFile(const std::filesystem::path& file) {
  std::ifstream ifs(file);
  if (!ifs.is_open()) return;

  std::string line;
  int line_num = 0;
  std::string paragraph;
  int para_start = 1;

  auto flush_paragraph = [&]() {
    if (paragraph.empty()) return;
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
    if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
      flush_paragraph();
      para_start = line_num + 1;
      continue;
    }

    if (paragraph.empty()) {
      para_start = line_num;
    }
    if (!paragraph.empty()) paragraph += "\n";
    paragraph += line;
  }
  flush_paragraph();
}

std::vector<MemorySearchResult> MemorySearch::Search(
    const std::string& query, int max_results) const {
  auto query_tokens = tokenize(query);
  if (query_tokens.empty()) return {};

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
  int count = std::min(max_results, static_cast<int>(scored.size()));
  for (int i = 0; i < count; ++i) {
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

void MemorySearch::Clear() {
  entries_.clear();
  total_documents_ = 0;
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

double MemorySearch::score_entry(
    const IndexEntry& entry,
    const std::vector<std::string>& query_tokens) const {
  if (entry.tokens.empty()) return 0;

  // Build term frequency map for the entry
  std::unordered_map<std::string, int> tf;
  for (const auto& t : entry.tokens) {
    tf[t]++;
  }

  // Count matching query tokens and compute TF-IDF-like score
  double score = 0;
  int matches = 0;

  for (const auto& qt : query_tokens) {
    auto it = tf.find(qt);
    if (it != tf.end()) {
      matches++;
      // TF component: log(1 + count)
      score += std::log(1.0 + it->second);
    }
  }

  if (matches == 0) return 0;

  // Coverage bonus: fraction of query tokens matched
  double coverage = static_cast<double>(matches) / query_tokens.size();
  score *= coverage;

  // Normalize by document length (shorter docs score higher for same matches)
  score /= std::log(2.0 + entry.tokens.size());

  return score;
}

}  // namespace quantclaw
