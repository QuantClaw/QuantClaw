// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <filesystem>
#include <fstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/core/mmr_reranker.hpp"
#include "quantclaw/core/temporal_decay.hpp"
#include "quantclaw/core/vector_index.hpp"
#include "quantclaw/providers/embedding_provider.hpp"

#include <gtest/gtest.h>

namespace quantclaw {

// ================================================================
// Mock Embedding Provider
// ================================================================

class MockEmbeddingProvider : public EmbeddingProvider {
 public:
  int dims_ = 4;

  EmbeddingResponse Embed(const EmbeddingRequest& request) override {
    EmbeddingResponse resp;
    const size_t dims = static_cast<size_t>(dims_);
    for (const auto& text : request.texts) {
      // Simple deterministic embedding: hash-based
      std::vector<float> emb(dims, 0.0f);
      for (size_t i = 0; i < text.size(); ++i) {
        emb[i % dims] += static_cast<float>(text[i]) / 100.0f;
      }
      // Normalize
      float norm = 0;
      for (float v : emb)
        norm += v * v;
      norm = std::sqrt(norm);
      if (norm > 0) {
        for (float& v : emb)
          v /= norm;
      }
      resp.embeddings.push_back(std::move(emb));
    }
    resp.total_tokens = static_cast<int>(request.texts.size()) * 10;
    return resp;
  }

  int Dimensions() const override {
    return dims_;
  }
  std::string Name() const override {
    return "mock";
  }
};

// ================================================================
// VectorIndex Tests
// ================================================================

class VectorIndexTest : public ::testing::Test {};

TEST_F(VectorIndexTest, AddAndSearch) {
  VectorIndex index;
  index.Add({"a", {1, 0, 0, 0}, "hello world", "file1.md", 1});
  index.Add({"b", {0, 1, 0, 0}, "goodbye moon", "file2.md", 1});
  index.Add({"c", {0.9f, 0.1f, 0, 0}, "hello there", "file3.md", 1});

  auto results = index.Search({1, 0, 0, 0}, 2);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].id, "a");  // Most similar
  EXPECT_GT(results[0].score, results[1].score);
}

TEST_F(VectorIndexTest, CosineSimilarityIdentical) {
  float sim = VectorIndex::CosineSimilarity({1, 0, 0}, {1, 0, 0});
  EXPECT_NEAR(sim, 1.0f, 1e-5);
}

TEST_F(VectorIndexTest, CosineSimilarityOrthogonal) {
  float sim = VectorIndex::CosineSimilarity({1, 0, 0}, {0, 1, 0});
  EXPECT_NEAR(sim, 0.0f, 1e-5);
}

TEST_F(VectorIndexTest, CosineSimilarityOpposite) {
  float sim = VectorIndex::CosineSimilarity({1, 0, 0}, {-1, 0, 0});
  EXPECT_NEAR(sim, -1.0f, 1e-5);
}

TEST_F(VectorIndexTest, EmptyIndex) {
  VectorIndex index;
  auto results = index.Search({1, 0}, 5);
  EXPECT_TRUE(results.empty());
}

TEST_F(VectorIndexTest, ClearIndex) {
  VectorIndex index;
  index.Add({"a", {1, 0}, "test", "file.md", 1});
  EXPECT_EQ(index.Size(), 1u);
  index.Clear();
  EXPECT_EQ(index.Size(), 0u);
}

// ================================================================
// MMRReranker Tests
// ================================================================

class MMRRerankerTest : public ::testing::Test {};

TEST_F(MMRRerankerTest, RerankSelectsTopK) {
  std::vector<RankedItem> items;
  for (int i = 0; i < 10; i++) {
    items.push_back({"id" + std::to_string(i), "content " + std::to_string(i),
                     "file.md", i, 1.0 - i * 0.1});
  }

  auto result = MMRReranker::Rerank(items, 3, 1.0);  // Pure relevance
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].id, "id0");  // Highest score
}

TEST_F(MMRRerankerTest, RerankPromotesDiversity) {
  std::vector<RankedItem> items = {
      {"a", "the cat sat on the mat", "f1", 1, 0.9},
      {"b", "the cat sat on the rug", "f2", 2, 0.85},  // Similar to a
      {"c", "dogs like to play fetch", "f3", 3, 0.8},  // Different
  };

  // With diversity emphasis (low lambda)
  auto result = MMRReranker::Rerank(items, 2, 0.3);
  ASSERT_EQ(result.size(), 2u);

  // Should prefer diverse results
  bool has_c = false;
  for (const auto& r : result) {
    if (r.id == "c")
      has_c = true;
  }
  EXPECT_TRUE(has_c);
}

TEST_F(MMRRerankerTest, JaccardSimilarityIdentical) {
  double sim = MMRReranker::JaccardSimilarity("hello world", "hello world");
  EXPECT_NEAR(sim, 1.0, 1e-5);
}

TEST_F(MMRRerankerTest, JaccardSimilarityNoOverlap) {
  double sim = MMRReranker::JaccardSimilarity("hello world", "foo bar");
  EXPECT_NEAR(sim, 0.0, 1e-5);
}

TEST_F(MMRRerankerTest, JaccardSimilarityPartial) {
  double sim =
      MMRReranker::JaccardSimilarity("hello world foo", "hello bar foo");
  // Intersection: {hello, foo}, Union: {hello, world, foo, bar}
  EXPECT_NEAR(sim, 0.5, 1e-5);
}

TEST_F(MMRRerankerTest, EmptyItems) {
  auto result = MMRReranker::Rerank({}, 5);
  EXPECT_TRUE(result.empty());
}

// ================================================================
// TemporalDecay Tests
// ================================================================

class TemporalDecayTest : public ::testing::Test {};

TEST_F(TemporalDecayTest, ScoreFromAgeZero) {
  TemporalDecay decay(30.0);
  EXPECT_NEAR(decay.ScoreFromAge(0), 1.0, 1e-5);
}

TEST_F(TemporalDecayTest, ScoreFromAgeHalfLife) {
  TemporalDecay decay(30.0);
  EXPECT_NEAR(decay.ScoreFromAge(30.0), 0.5, 1e-5);
}

TEST_F(TemporalDecayTest, ScoreFromAgeTwoHalfLives) {
  TemporalDecay decay(30.0);
  EXPECT_NEAR(decay.ScoreFromAge(60.0), 0.25, 1e-5);
}

TEST_F(TemporalDecayTest, ScoreFromAgeNegative) {
  TemporalDecay decay(30.0);
  EXPECT_NEAR(decay.ScoreFromAge(-5.0), 1.0, 1e-5);
}

TEST_F(TemporalDecayTest, CustomHalfLife) {
  TemporalDecay decay(7.0);  // 1 week half-life
  EXPECT_NEAR(decay.ScoreFromAge(7.0), 0.5, 1e-5);
  EXPECT_NEAR(decay.HalfLifeDays(), 7.0, 1e-5);
}

TEST_F(TemporalDecayTest, ScoreFromTimePoint) {
  TemporalDecay decay(30.0);
  auto now = std::chrono::system_clock::now();
  // Score for "now" should be ~1.0
  double score = decay.Score(now);
  EXPECT_GT(score, 0.99);
}

// ================================================================
// HybridSearch Integration Tests
// ================================================================

class HybridSearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    // Create unique temp directory per test instance
    tmp_dir_ =
        std::filesystem::temp_directory_path() /
        ("qc_hybrid_test_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir_);

    // Create test memory files
    write_file(
        tmp_dir_ / "notes.md",
        "Machine learning models\n\nNeural networks and deep learning\n\n"
        "Gradient descent optimization\n\nData preprocessing pipeline\n");
    write_file(tmp_dir_ / "tasks.md",
               "Fix authentication bug\n\nUpdate API documentation\n\n"
               "Deploy new version to staging\n\nReview pull requests\n");
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir_, ec);
  }

  void write_file(const std::filesystem::path& path,
                  const std::string& content) {
    std::ofstream ofs(path);
    ofs << content;
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::filesystem::path tmp_dir_;
};

TEST_F(HybridSearchTest, FallbackToBM25WithoutEmbeddingProvider) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  HybridSearchOptions opts;
  opts.use_temporal_decay = false;
  opts.use_mmr = false;
  auto results = search.HybridSearch("machine learning", opts);

  EXPECT_FALSE(results.empty());
  EXPECT_NE(results[0].content.find("Machine learning"), std::string::npos);
}

TEST_F(HybridSearchTest, HybridWithMockEmbeddingProvider) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  auto provider = std::make_shared<MockEmbeddingProvider>();
  search.SetEmbeddingProvider(provider);
  search.BuildVectorIndex();

  HybridSearchOptions opts;
  opts.use_mmr = false;
  auto results = search.HybridSearch("neural networks", opts);

  EXPECT_FALSE(results.empty());
}

TEST_F(HybridSearchTest, HybridWithMMR) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  HybridSearchOptions opts;
  opts.use_mmr = true;
  opts.mmr_lambda = 0.7;
  opts.max_results = 3;
  auto results = search.HybridSearch("learning", opts);

  EXPECT_LE(static_cast<int>(results.size()), 3);
}

TEST_F(HybridSearchTest, HybridWithTemporalDecay) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  HybridSearchOptions opts;
  opts.use_temporal_decay = true;
  opts.use_mmr = false;
  auto results = search.HybridSearch("deploy", opts);

  // Recent files should score higher with temporal decay
  EXPECT_FALSE(results.empty());
  EXPECT_GT(results[0].score, 0);
}

TEST_F(HybridSearchTest, EmptyQueryReturnsEmpty) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  auto results = search.HybridSearch("");
  EXPECT_TRUE(results.empty());
}

TEST_F(HybridSearchTest, StatsIncludesIndexSize) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  auto stats = search.Stats();
  EXPECT_GT(stats["indexed_entries"].get<int>(), 0);
}

TEST_F(HybridSearchTest, ClearResetsAll) {
  MemorySearch search(logger_);
  search.IndexDirectory(tmp_dir_);

  auto provider = std::make_shared<MockEmbeddingProvider>();
  search.SetEmbeddingProvider(provider);
  search.BuildVectorIndex();

  search.Clear();
  auto stats = search.Stats();
  EXPECT_EQ(stats["indexed_entries"].get<int>(), 0);
}

}  // namespace quantclaw
