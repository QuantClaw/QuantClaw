// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/memory_search.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class MemoryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_memory_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    memory_manager_ =
        std::make_unique<quantclaw::MemoryManager>(test_dir_, logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::MemoryManager> memory_manager_;
};

TEST_F(MemoryManagerTest, ReadIdentityFile) {
  std::ofstream f(test_dir_ / "SOUL.md");
  f << "# My Soul\n\nI am a helpful assistant.";
  f.close();

  auto content = memory_manager_->ReadIdentityFile("SOUL.md");
  EXPECT_EQ(content, "# My Soul\n\nI am a helpful assistant.");
}

TEST_F(MemoryManagerTest, ReadNonExistentFile) {
  EXPECT_THROW(memory_manager_->ReadIdentityFile("NONEXISTENT.md"),
               std::runtime_error);
}

TEST_F(MemoryManagerTest, ReadAgentsFile) {
  std::ofstream f(test_dir_ / "AGENTS.md");
  f << "# Agent Behavior\nBe concise.";
  f.close();

  auto content = memory_manager_->ReadAgentsFile();
  EXPECT_EQ(content, "# Agent Behavior\nBe concise.");
}

TEST_F(MemoryManagerTest, ReadToolsFile) {
  std::ofstream f(test_dir_ / "TOOLS.md");
  f << "# Tool Guide\nUse read for files.";
  f.close();

  auto content = memory_manager_->ReadToolsFile();
  EXPECT_EQ(content, "# Tool Guide\nUse read for files.");
}

TEST_F(MemoryManagerTest, SaveDailyMemory) {
  memory_manager_->SaveDailyMemory("This is a test memory entry.");

  auto memory_dir = test_dir_ / "memory";
  EXPECT_TRUE(std::filesystem::exists(memory_dir));

  bool found = false;
  for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
    if (entry.path().extension() == ".md") {
      std::ifstream file(entry.path());
      std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
      if (content.find("This is a test memory entry.") != std::string::npos) {
        found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(MemoryManagerTest, SearchMemory) {
  std::ofstream soul(test_dir_ / "SOUL.md");
  soul << "I love quantum physics and lobsters.";
  soul.close();

  std::ofstream user(test_dir_ / "USER.md");
  user << "The user is interested in AI and C++ programming.";
  user.close();

  auto results = memory_manager_->SearchMemory("quantum");
  EXPECT_FALSE(results.empty());
  EXPECT_TRUE(results[0].find("quantum") != std::string::npos);

  auto no_results = memory_manager_->SearchMemory("zzz_nonexistent_zzz");
  EXPECT_TRUE(no_results.empty());
}

TEST_F(MemoryManagerTest, GetWorkspacePath) {
  EXPECT_EQ(memory_manager_->GetWorkspacePath(), test_dir_);
}

TEST_F(MemoryManagerTest, GetSessionsDir) {
  auto sessions_dir = memory_manager_->GetSessionsDir("main");
  EXPECT_TRUE(sessions_dir.string().find("agents/main/sessions") !=
              std::string::npos);
}

TEST_F(MemoryManagerTest, LoadWorkspaceFiles) {
  std::ofstream f(test_dir_ / "SOUL.md");
  f << "test soul";
  f.close();

  // Should not throw
  EXPECT_NO_THROW(memory_manager_->LoadWorkspaceFiles());
}

// --- File watcher tests ---

TEST_F(MemoryManagerTest, FileWatcherStartStop) {
  // Should not throw
  EXPECT_NO_THROW(memory_manager_->StartFileWatcher());
  EXPECT_NO_THROW(memory_manager_->StopFileWatcher());
}

TEST_F(MemoryManagerTest, FileWatcherDetectsChange) {
  // Create initial file
  {
    std::ofstream f(test_dir_ / "SOUL.md");
    f << "initial content";
  }

  bool changed = false;
  std::string changed_file;
  memory_manager_->SetFileChangeCallback([&](const std::string& filename) {
    changed = true;
    changed_file = filename;
  });

  memory_manager_->StartFileWatcher();

  // Wait for initial scan + ensure mtime granularity (>1s on some FS)
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Modify the file
  {
    std::ofstream f(test_dir_ / "SOUL.md");
    f << "modified content";
  }

  // Wait for watcher to detect (polls every 5 seconds, plus margin for CI)
  std::this_thread::sleep_for(std::chrono::seconds(12));

  memory_manager_->StopFileWatcher();

  EXPECT_TRUE(changed);
  EXPECT_EQ(changed_file, "SOUL.md");
}

TEST_F(MemoryManagerTest, FileWatcherDoubleStartIgnored) {
  memory_manager_->StartFileWatcher();
  // Second start should be a no-op
  EXPECT_NO_THROW(memory_manager_->StartFileWatcher());
  memory_manager_->StopFileWatcher();
}

// ================================================================
// P4 — MemorySearch Extended Tests
// ================================================================

class MemorySearchExtendedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_memsearch_test");
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("memsearch_test", null_sink);
    search_ = std::make_unique<quantclaw::MemorySearch>(logger);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  void write_file(const std::string& name, const std::string& content) {
    auto path = test_dir_ / name;
    // Create parent directories if needed
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << content;
  }

  std::unique_ptr<quantclaw::MemorySearch> search_;
  std::filesystem::path test_dir_;
};

TEST_F(MemorySearchExtendedTest, ParagraphBoundaryIndexing) {
  // File with 3 paragraphs separated by blank lines -> 3 separate entries
  write_file("paragraphs.md",
             "First paragraph about quantum computing.\n"
             "\n"
             "Second paragraph about machine learning.\n"
             "\n"
             "Third paragraph about distributed systems.\n");

  search_->IndexDirectory(test_dir_);

  auto stats = search_->Stats();
  EXPECT_EQ(stats["indexed_entries"].get<int>(), 3);
}

TEST_F(MemorySearchExtendedTest, SkipsShortTokens) {
  // Single-character words are excluded from indexing
  write_file("short_tokens.md", "I a x y z am the great");
  search_->IndexDirectory(test_dir_);

  // Search for single-char tokens should return nothing
  auto results = search_->Search("a");
  EXPECT_TRUE(results.empty());

  // Search for a multi-char token should work
  auto results2 = search_->Search("great");
  EXPECT_FALSE(results2.empty());
}

TEST_F(MemorySearchExtendedTest, FileExtensionFilter) {
  // Only .md, .txt, .jsonl files are indexed (not .jpg, .cpp)
  write_file("included.md", "markdown document about testing");
  write_file("included.txt", "text file about testing");
  write_file("included.jsonl", "jsonl data about testing");
  write_file("excluded.jpg", "image data about testing");
  write_file("excluded.cpp", "code about testing");
  write_file("excluded.py", "python about testing");

  search_->IndexDirectory(test_dir_);

  auto results = search_->Search("testing");

  // All results should come from .md, .txt, or .jsonl files
  for (const auto& r : results) {
    auto ext = std::filesystem::path(r.source).extension().string();
    EXPECT_TRUE(ext == ".md" || ext == ".txt" || ext == ".jsonl")
        << "Unexpected extension indexed: " << ext;
  }

  // Should have results from the included files
  EXPECT_GE(results.size(), 3u);
}

TEST_F(MemorySearchExtendedTest, ClearResetsIndex) {
  write_file("test.md", "searchable content here");
  search_->IndexDirectory(test_dir_);
  EXPECT_FALSE(search_->Search("searchable").empty());

  search_->Clear();

  auto stats = search_->Stats();
  EXPECT_EQ(stats["indexed_entries"], 0);
  EXPECT_EQ(stats["total_documents"], 0);

  auto results = search_->Search("searchable");
  EXPECT_TRUE(results.empty());
}

TEST_F(MemorySearchExtendedTest, MultiFileSearch) {
  // Index 2 files, search returns results from both with correct source paths
  write_file("alpha.md", "quantum entanglement experiment results");
  write_file("beta.md", "quantum computing hardware advances");

  search_->IndexDirectory(test_dir_);

  auto results = search_->Search("quantum");
  ASSERT_GE(results.size(), 2u);

  // Verify both files appear in results with correct paths
  bool found_alpha = false;
  bool found_beta = false;
  for (const auto& r : results) {
    if (r.source.find("alpha.md") != std::string::npos)
      found_alpha = true;
    if (r.source.find("beta.md") != std::string::npos)
      found_beta = true;
  }
  EXPECT_TRUE(found_alpha);
  EXPECT_TRUE(found_beta);
}
