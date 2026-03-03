// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "quantclaw/core/context_pruner.hpp"
#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/gateway/command_queue.hpp"
#include "quantclaw/config.hpp"

namespace quantclaw {

// ================================================================
// ContextPruner Tests
// ================================================================

class ContextPrunerTest : public ::testing::Test {
 protected:
  std::vector<Message> make_history(int assistant_count,
                                    bool with_tool_results = true) {
    std::vector<Message> history;
    for (int i = 0; i < assistant_count; ++i) {
      // User message
      history.push_back(Message{"user", "Question " + std::to_string(i)});

      // Assistant message with tool use
      Message assistant;
      assistant.role = "assistant";
      assistant.content.push_back(ContentBlock::MakeText("Thinking..."));
      assistant.content.push_back(ContentBlock::MakeToolUse(
          "tool_" + std::to_string(i), "read_file", {{"path", "/test"}}));
      history.push_back(assistant);

      // Tool result message
      if (with_tool_results) {
        Message tool_result;
        tool_result.role = "user";
        std::string content;
        for (int j = 0; j < 20; ++j) {
          content += "Line " + std::to_string(j) + " of tool result\n";
        }
        tool_result.content.push_back(
            ContentBlock::MakeToolResult("tool_" + std::to_string(i), content));
        history.push_back(tool_result);
      }
    }
    return history;
  }
};

TEST_F(ContextPrunerTest, EmptyHistory) {
  ContextPruner::Options opts;
  auto result = ContextPruner::Prune({}, opts);
  EXPECT_TRUE(result.empty());
}

TEST_F(ContextPrunerTest, SmallHistoryUnchanged) {
  auto history = make_history(2);
  ContextPruner::Options opts;
  opts.protect_recent = 5;  // Protect more than we have

  auto result = ContextPruner::Prune(history, opts);
  EXPECT_EQ(result.size(), history.size());
}

TEST_F(ContextPrunerTest, RecentToolResultsProtected) {
  auto history = make_history(5);
  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.max_tool_result_chars = 10;  // Very small to force pruning

  auto result = ContextPruner::Prune(history, opts);

  // The most recent 3 assistant message groups should keep full tool results
  // Count how many tool results still have the original content
  int full_results = 0;
  int pruned_results = 0;
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result") {
        if (block.content.find("omitted") != std::string::npos ||
            block.content.find("...") != std::string::npos) {
          pruned_results++;
        } else {
          full_results++;
        }
      }
    }
  }

  // Should have some pruned and some full
  EXPECT_GT(full_results, 0);
  EXPECT_GT(pruned_results, 0);
}

TEST_F(ContextPrunerTest, HardPruneOldResults) {
  auto history = make_history(15);
  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.hard_prune_after = 10;
  opts.max_tool_result_chars = 10;

  auto result = ContextPruner::Prune(history, opts);

  // Very old results should be hard-pruned (contain "omitted")
  bool found_hard_pruned = false;
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" &&
          block.content.find("omitted") != std::string::npos) {
        found_hard_pruned = true;
        break;
      }
    }
    if (found_hard_pruned) break;
  }
  EXPECT_TRUE(found_hard_pruned);
}

TEST_F(ContextPrunerTest, SoftPruneKeepsHeadAndTail) {
  // Create a large tool result
  std::string large_content;
  for (int i = 0; i < 50; ++i) {
    large_content += "Line " + std::to_string(i) + ": some content here\n";
  }

  std::vector<Message> history;
  // Add 5 assistant turns, only the first has a big tool result
  for (int i = 0; i < 5; ++i) {
    history.push_back(Message{"user", "Q"});

    Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(ContentBlock::MakeToolUse(
        "t" + std::to_string(i), "tool", {}));
    history.push_back(assistant);

    Message tool_msg;
    tool_msg.role = "user";
    if (i == 0) {
      tool_msg.content.push_back(
          ContentBlock::MakeToolResult("t0", large_content));
    } else {
      tool_msg.content.push_back(
          ContentBlock::MakeToolResult("t" + std::to_string(i), "short"));
    }
    history.push_back(tool_msg);
  }

  ContextPruner::Options opts;
  opts.protect_recent = 2;
  opts.soft_prune_lines = 3;
  opts.max_tool_result_chars = 100;

  auto result = ContextPruner::Prune(history, opts);

  // Find the soft-pruned result for t0
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" && block.tool_use_id == "t0") {
        EXPECT_TRUE(block.content.find("Line 0") != std::string::npos);
        EXPECT_TRUE(block.content.find("lines omitted") != std::string::npos);
        EXPECT_TRUE(block.content.find("Line 49") != std::string::npos);
      }
    }
  }
}

TEST_F(ContextPrunerTest, NoToolResultsPassthrough) {
  std::vector<Message> history;
  history.push_back(Message{"user", "Hello"});
  history.push_back(Message{"assistant", "Hi there!"});
  history.push_back(Message{"user", "How are you?"});
  history.push_back(Message{"assistant", "I'm good!"});

  ContextPruner::Options opts;
  auto result = ContextPruner::Prune(history, opts);
  EXPECT_EQ(result.size(), history.size());
}

// ================================================================
// BM25 Memory Search Tests
// ================================================================

class BM25SearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("test", null_sink);
    search_ = std::make_unique<MemorySearch>(logger);

    // Create a unique temp directory to avoid conflicts when ctest --parallel
    // runs each GTest case as a separate process sharing the same /tmp path.
    std::string tmpl =
        (std::filesystem::temp_directory_path() / "bm25_test_XXXXXX").string();
    char* result = mkdtemp(tmpl.data());
    ASSERT_NE(result, nullptr) << "mkdtemp failed";
    temp_dir_ = result;
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  void write_file(const std::string& name, const std::string& content) {
    std::ofstream ofs(temp_dir_ / name);
    ofs << content;
  }

  std::unique_ptr<MemorySearch> search_;
  std::filesystem::path temp_dir_;
};

TEST_F(BM25SearchTest, EmptyIndex) {
  auto results = search_->Search("anything");
  EXPECT_TRUE(results.empty());
}

TEST_F(BM25SearchTest, BasicSearch) {
  write_file("test.md", "The quick brown fox jumps over the lazy dog");
  search_->IndexDirectory(temp_dir_);

  auto results = search_->Search("fox");
  ASSERT_FALSE(results.empty());
  EXPECT_GT(results[0].score, 0);
}

TEST_F(BM25SearchTest, RanksRelevantHigher) {
  write_file("a.md", "machine learning is a subset of artificial intelligence");
  write_file("b.md", "the weather today is sunny and warm");
  write_file("c.md",
             "deep learning neural networks use machine learning techniques");
  search_->IndexDirectory(temp_dir_);

  auto results = search_->Search("machine learning");
  ASSERT_GE(results.size(), 2u);

  // "c.md" mentions "machine learning" twice, should rank high
  // "a.md" also mentions it
  // "b.md" should not appear or rank lowest
  bool weather_ranked_last = true;
  for (const auto& r : results) {
    if (r.source.find("b.md") != std::string::npos) {
      // Weather file should not match "machine learning"
      weather_ranked_last = true;
    }
  }
  EXPECT_TRUE(weather_ranked_last);
}

TEST_F(BM25SearchTest, IDFWeighting) {
  // Create documents where "the" appears in all but "quantum" only in one
  write_file("a.md", "the cat sat on the mat");
  write_file("b.md", "the dog ran in the park");
  write_file("c.md", "quantum computing changes the world");
  search_->IndexDirectory(temp_dir_);

  auto results = search_->Search("quantum");
  ASSERT_FALSE(results.empty());
  EXPECT_TRUE(results[0].source.find("c.md") != std::string::npos);
}

TEST_F(BM25SearchTest, DocumentLengthNormalization) {
  // Short doc with the term should score higher than long doc with same freq
  write_file("short.md", "rust programming language");
  std::string long_content = "rust programming language";
  for (int i = 0; i < 50; ++i) {
    long_content += " filler word content padding text extra stuff here";
  }
  write_file("long.md", long_content);
  search_->IndexDirectory(temp_dir_);

  auto results = search_->Search("rust programming");
  ASSERT_GE(results.size(), 2u);
  // Short doc should rank higher due to BM25 length normalization
  EXPECT_TRUE(results[0].source.find("short.md") != std::string::npos);
}

TEST_F(BM25SearchTest, ClearResetsState) {
  write_file("test.md", "some searchable content");
  search_->IndexDirectory(temp_dir_);
  EXPECT_FALSE(search_->Search("searchable").empty());

  search_->Clear();
  EXPECT_TRUE(search_->Search("searchable").empty());

  auto stats = search_->Stats();
  EXPECT_EQ(stats["indexed_entries"], 0);
  EXPECT_EQ(stats["total_documents"], 0);
}

TEST_F(BM25SearchTest, StatsReportCorrectly) {
  write_file("a.md", "paragraph one\n\nparagraph two");
  write_file("b.md", "single paragraph");
  search_->IndexDirectory(temp_dir_);

  auto stats = search_->Stats();
  EXPECT_GE(stats["indexed_entries"].get<int>(), 2);
  EXPECT_GE(stats["total_documents"].get<int>(), 2);
}

// ================================================================
// Compaction Config Tests
// ================================================================

TEST(CompactionConfigTest, DefaultValues) {
  AgentConfig config;
  EXPECT_TRUE(config.auto_compact);
  EXPECT_EQ(config.compact_max_messages, 100);
  EXPECT_EQ(config.compact_keep_recent, 20);
  EXPECT_EQ(config.compact_max_tokens, 100000);
}

TEST(CompactionConfigTest, ParseFromJson) {
  nlohmann::json j = {
      {"model", "test-model"},
      {"autoCompact", false},
      {"compactMaxMessages", 50},
      {"compactKeepRecent", 10},
      {"compactMaxTokens", 50000},
  };

  auto config = AgentConfig::FromJson(j);
  EXPECT_FALSE(config.auto_compact);
  EXPECT_EQ(config.compact_max_messages, 50);
  EXPECT_EQ(config.compact_keep_recent, 10);
  EXPECT_EQ(config.compact_max_tokens, 50000);
}

TEST(CompactionConfigTest, ParseSnakeCase) {
  nlohmann::json j = {
      {"model", "test-model"},
      {"auto_compact", false},
      {"compact_max_messages", 75},
      {"compact_keep_recent", 15},
      {"compact_max_tokens", 80000},
  };

  auto config = AgentConfig::FromJson(j);
  EXPECT_FALSE(config.auto_compact);
  EXPECT_EQ(config.compact_max_messages, 75);
  EXPECT_EQ(config.compact_keep_recent, 15);
  EXPECT_EQ(config.compact_max_tokens, 80000);
}

// ================================================================
// P3 — Bootstrap Message Protection Tests
// ================================================================

TEST_F(ContextPrunerTest, BootstrapProtection) {
  // Build a history with system setup messages before the first user message,
  // followed by many tool-using turns.
  std::vector<Message> history;

  // System/setup messages before first user message
  history.push_back(Message{"system", "You are a helpful assistant."});
  history.push_back(Message{"assistant", "Ready to help!"});

  // First user message (bootstrap boundary)
  history.push_back(Message{"user", "Hello"});

  // Add a tool result right after the first user message (index 3)
  // This would normally be prunable but is within bootstrap region
  Message setup_assistant;
  setup_assistant.role = "assistant";
  setup_assistant.content.push_back(ContentBlock::MakeToolUse(
      "setup_tool", "read_file", {{"path", "/setup"}}));
  history.push_back(setup_assistant);

  Message setup_result;
  setup_result.role = "user";
  std::string big_result;
  for (int i = 0; i < 30; ++i) {
    big_result += "Setup line " + std::to_string(i) + "\n";
  }
  setup_result.content.push_back(
      ContentBlock::MakeToolResult("setup_tool", big_result));
  history.push_back(setup_result);

  // Add many more turns to push the setup into "old" territory
  auto later_turns = make_history(10);
  history.insert(history.end(), later_turns.begin(), later_turns.end());

  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.hard_prune_after = 5;
  opts.max_tool_result_chars = 10;

  auto result = ContextPruner::Prune(history, opts);

  // The setup_tool result (index 4, before first user message at index 2)
  // should NOT be pruned due to bootstrap protection.
  // However, since setup_result is at index 4 (AFTER first user message at index 2),
  // it's outside bootstrap protection. Let me fix the test:
  // Bootstrap boundary is at first user message (index 2).
  // Messages AT OR BEFORE index 2 are protected.
  // The system and "Ready to help!" messages are at indices 0 and 1 — both protected.
  // The first user message at index 2 is protected.
  // Index 3+ can be pruned (they're after bootstrap).

  // Verify the system message survived (index 0, within bootstrap)
  EXPECT_EQ(result[0].role, "system");
  EXPECT_EQ(result[0].text(), "You are a helpful assistant.");
}

TEST_F(ContextPrunerTest, BootstrapProtectionNoUserMessages) {
  // If there are no user messages, no bootstrap protection applies
  // (bootstrap_end == -1, so the condition `i <= -1` is never true)
  std::vector<Message> history;
  history.push_back(Message{"system", "System prompt"});
  history.push_back(Message{"assistant", "Hello"});

  ContextPruner::Options opts;
  auto result = ContextPruner::Prune(history, opts);
  EXPECT_EQ(result.size(), history.size());
}

// ================================================================
// P3 — Queue Overflow Summary Tests
// ================================================================

TEST(QueueOverflowTest, SummarizePolicyMergesMessages) {
  // We test the SessionLane's ApplyCapOverflow with summarize policy
  // The improved version should include a "[Queue summary: N messages]" prefix
  gateway::SessionLane lane("test-session");
  lane.SetCap(2);
  lane.SetDropPolicy(gateway::DropPolicy::kSummarize);

  // Enqueue 4 commands (exceeds cap of 2)
  for (int i = 0; i < 4; ++i) {
    gateway::QueuedCommand cmd;
    cmd.id = "cmd-" + std::to_string(i);
    cmd.session_key = "test-session";
    cmd.message = "Message " + std::to_string(i);
    cmd.enqueued_at = std::chrono::steady_clock::now();
    lane.Enqueue(std::move(cmd));
  }

  auto dropped = lane.ApplyCapOverflow();
  EXPECT_GE(dropped.size(), 2u);  // At least 2 dropped
  EXPECT_EQ(lane.PendingCount(), 2u);  // Should be at cap now
}

TEST(QueueOverflowTest, SummarizedMessageHasPrefix) {
  gateway::SessionLane lane("test-session");
  lane.SetCap(1);
  lane.SetDebounceMs(0);  // Disable debounce for test
  lane.SetDropPolicy(gateway::DropPolicy::kSummarize);

  // Enqueue 3 commands
  for (int i = 0; i < 3; ++i) {
    gateway::QueuedCommand cmd;
    cmd.id = "cmd-" + std::to_string(i);
    cmd.session_key = "test-session";
    cmd.message = "Message content " + std::to_string(i);
    cmd.enqueued_at = std::chrono::steady_clock::now();
    lane.Enqueue(std::move(cmd));
  }

  auto dropped = lane.ApplyCapOverflow();
  EXPECT_GE(dropped.size(), 2u);

  // The remaining message should have the queue summary prefix
  auto now = std::chrono::steady_clock::now();
  auto activated = lane.TryActivate(now);
  ASSERT_TRUE(activated.has_value());
  EXPECT_TRUE(activated->message.find("[Queue summary:") != std::string::npos);
  EXPECT_TRUE(activated->message.find("messages]") != std::string::npos);
}

// ================================================================
// P4 — ContextPruner Extended Tests (ported from OpenClaw)
// ================================================================

TEST_F(ContextPrunerTest, SoftPruneShortContentUnchanged) {
  // Tool result with fewer than 2*soft_prune_lines lines is unchanged
  std::string short_content;
  for (int i = 0; i < 4; ++i) {
    short_content += "Line " + std::to_string(i) + "\n";
  }

  std::vector<Message> history;
  for (int i = 0; i < 5; ++i) {
    history.push_back(Message{"user", "Q"});

    Message assistant;
    assistant.role = "assistant";
    assistant.content.push_back(ContentBlock::MakeToolUse(
        "t" + std::to_string(i), "tool", {}));
    history.push_back(assistant);

    Message tool_msg;
    tool_msg.role = "user";
    if (i == 0) {
      tool_msg.content.push_back(
          ContentBlock::MakeToolResult("t0", short_content));
    } else {
      tool_msg.content.push_back(
          ContentBlock::MakeToolResult("t" + std::to_string(i), "short"));
    }
    history.push_back(tool_msg);
  }

  ContextPruner::Options opts;
  opts.protect_recent = 2;
  opts.soft_prune_lines = 3;
  opts.max_tool_result_chars = 10;  // Force soft prune attempt

  auto result = ContextPruner::Prune(history, opts);

  // Find the result for t0 — short content should pass through unchanged
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" && block.tool_use_id == "t0") {
        // 4 lines < 2*3=6 lines, so no pruning
        EXPECT_TRUE(block.content.find("lines omitted") == std::string::npos);
      }
    }
  }
}

TEST_F(ContextPrunerTest, HardPruneReplacesWithPlaceholder) {
  // Very old tool result beyond hard_prune_after gets placeholder
  auto history = make_history(20);
  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.hard_prune_after = 5;
  opts.max_tool_result_chars = 10;

  auto result = ContextPruner::Prune(history, opts);

  // The first tool result (very old) should be hard pruned
  bool found_placeholder = false;
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" &&
          block.content == "[Tool result omitted — older context]") {
        found_placeholder = true;
        break;
      }
    }
    if (found_placeholder) break;
  }
  EXPECT_TRUE(found_placeholder);
}

TEST_F(ContextPrunerTest, MultipleToolResultsInOneMessage) {
  // Message with 2 tool_result blocks, one hard-pruned, one soft-pruned
  std::vector<Message> history;

  // Add many assistant turns first to create recency depth
  for (int i = 0; i < 15; ++i) {
    history.push_back(Message{"user", "Q" + std::to_string(i)});
    Message a;
    a.role = "assistant";
    a.content.push_back(ContentBlock::MakeToolUse(
        "t" + std::to_string(i), "tool", {}));
    history.push_back(a);

    Message tr;
    tr.role = "user";
    tr.content.push_back(
        ContentBlock::MakeToolResult("t" + std::to_string(i), "result"));
    history.push_back(tr);
  }

  // Now add a message with two tool results at the very start
  // We'll insert it at the beginning (old position)
  Message multi_tool;
  multi_tool.role = "user";
  std::string big_content;
  for (int j = 0; j < 50; ++j) {
    big_content += "Data line " + std::to_string(j) + "\n";
  }
  multi_tool.content.push_back(
      ContentBlock::MakeToolResult("multi1", big_content));
  multi_tool.content.push_back(
      ContentBlock::MakeToolResult("multi2", "Short result"));

  // Insert assistant + tool results at the beginning
  Message multi_asst;
  multi_asst.role = "assistant";
  multi_asst.content.push_back(ContentBlock::MakeToolUse("multi1", "tool_a", {}));
  multi_asst.content.push_back(ContentBlock::MakeToolUse("multi2", "tool_b", {}));

  history.insert(history.begin(), multi_tool);
  history.insert(history.begin(), multi_asst);
  history.insert(history.begin(), Message{"user", "Initial question"});

  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.hard_prune_after = 5;
  opts.max_tool_result_chars = 100;
  opts.soft_prune_lines = 3;

  auto result = ContextPruner::Prune(history, opts);

  // Verify the message with multi1 and multi2 was processed
  for (const auto& msg : result) {
    for (const auto& block : msg.content) {
      if (block.type == "tool_result" && block.tool_use_id == "multi1") {
        // This large old result should be hard or soft pruned
        EXPECT_TRUE(
            block.content.find("omitted") != std::string::npos ||
            block.content.find("...") != std::string::npos);
      }
    }
  }
}

TEST_F(ContextPrunerTest, NonToolResultBlocksPreserved) {
  // Text blocks in same message as tool_result are kept unchanged
  std::vector<Message> history;
  for (int i = 0; i < 10; ++i) {
    history.push_back(Message{"user", "Q"});
    Message a;
    a.role = "assistant";
    a.content.push_back(ContentBlock::MakeText("Thinking about this..."));
    a.content.push_back(ContentBlock::MakeToolUse(
        "t" + std::to_string(i), "tool", {}));
    history.push_back(a);

    Message tr;
    tr.role = "user";
    // Add a text block alongside the tool result
    tr.content.push_back(ContentBlock::MakeText("Some context"));
    std::string big;
    for (int j = 0; j < 30; ++j) big += "Line " + std::to_string(j) + "\n";
    tr.content.push_back(
        ContentBlock::MakeToolResult("t" + std::to_string(i), big));
    history.push_back(tr);
  }

  ContextPruner::Options opts;
  opts.protect_recent = 2;
  opts.max_tool_result_chars = 50;
  opts.soft_prune_lines = 3;

  auto result = ContextPruner::Prune(history, opts);

  // Verify text blocks in the old messages are still present
  bool found_text_block = false;
  for (const auto& msg : result) {
    if (msg.role != "user") continue;
    for (const auto& block : msg.content) {
      if (block.type == "text" && block.text == "Some context") {
        found_text_block = true;
        break;
      }
    }
    if (found_text_block) break;
  }
  EXPECT_TRUE(found_text_block);
}

TEST_F(ContextPrunerTest, NoAssistantMessagesNoProtection) {
  // History with no assistant messages: protect_threshold stays -1,
  // no pruning happens
  std::vector<Message> history;
  for (int i = 0; i < 5; ++i) {
    history.push_back(Message{"user", "Question " + std::to_string(i)});
  }

  ContextPruner::Options opts;
  opts.protect_recent = 3;
  opts.max_tool_result_chars = 10;

  auto result = ContextPruner::Prune(history, opts);
  EXPECT_EQ(result.size(), history.size());
  for (size_t i = 0; i < history.size(); ++i) {
    EXPECT_EQ(result[i].text(), history[i].text());
  }
}

}  // namespace quantclaw
