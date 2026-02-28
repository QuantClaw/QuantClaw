#pragma once

#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace quantclaw {

struct MemorySearchResult {
  std::string source;     // file path
  std::string content;    // matching line/paragraph
  double score;           // relevance score (0-1)
  int line_number;        // line number in source
};

// Full-text memory search across workspace memory files.
// Supports keyword matching with TF-IDF-like scoring.
class MemorySearch {
 public:
  explicit MemorySearch(std::shared_ptr<spdlog::logger> logger);

  // Index memory files from a workspace directory
  void index_directory(const std::filesystem::path& dir);

  // Add a single file to the index
  void index_file(const std::filesystem::path& file);

  // Search for relevant memory entries
  std::vector<MemorySearchResult> search(const std::string& query,
                                          int max_results = 10) const;

  // Get index stats
  nlohmann::json stats() const;

  // Clear the index
  void clear();

 private:
  struct IndexEntry {
    std::string filepath;
    int line_number;
    std::string content;
    std::vector<std::string> tokens;
  };

  // Tokenize text into lowercase words
  static std::vector<std::string> tokenize(const std::string& text);

  // Score a document against query tokens
  double score_entry(const IndexEntry& entry,
                     const std::vector<std::string>& query_tokens) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::vector<IndexEntry> entries_;
  int total_documents_ = 0;
};

}  // namespace quantclaw
