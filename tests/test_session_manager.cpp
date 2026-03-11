// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/session/session_manager.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class SessionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_session_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    session_mgr_ =
        std::make_unique<quantclaw::SessionManager>(test_dir_, logger_);
  }

  void TearDown() override {
    session_mgr_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SessionManager> session_mgr_;
};

// --- get_or_create ---

TEST_F(SessionManagerTest, CreateNewSession) {
  auto handle = session_mgr_->GetOrCreate("agent:main:main", "Main", "cli");

  EXPECT_EQ(handle.session_key, "agent:main:main");
  EXPECT_FALSE(handle.session_id.empty());
  EXPECT_TRUE(std::filesystem::exists(test_dir_ / "sessions.json"));
}

TEST_F(SessionManagerTest, GetExistingSession) {
  auto h1 = session_mgr_->GetOrCreate("agent:main:main");
  auto h2 = session_mgr_->GetOrCreate("agent:main:main");

  EXPECT_EQ(h1.session_id, h2.session_id);
}

TEST_F(SessionManagerTest, DifferentKeysCreateDifferentSessions) {
  auto h1 = session_mgr_->GetOrCreate("agent:main:main");
  auto h2 = session_mgr_->GetOrCreate("agent:main:dm:user1");

  EXPECT_NE(h1.session_id, h2.session_id);
}

// --- append_message / get_history ---

TEST_F(SessionManagerTest, AppendAndRetrieveMessages) {
  session_mgr_->GetOrCreate("test:session");

  session_mgr_->AppendMessage("test:session", "user", "Hello");
  session_mgr_->AppendMessage("test:session", "assistant", "Hi there!");

  auto history = session_mgr_->GetHistory("test:session");

  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].role, "user");
  EXPECT_EQ(history[0].content[0].text, "Hello");
  EXPECT_EQ(history[1].role, "assistant");
  EXPECT_EQ(history[1].content[0].text, "Hi there!");
}

TEST_F(SessionManagerTest, MessageTimestampsAreSet) {
  session_mgr_->GetOrCreate("test:ts");
  session_mgr_->AppendMessage("test:ts", "user", "test");

  auto history = session_mgr_->GetHistory("test:ts");
  ASSERT_EQ(history.size(), 1u);
  EXPECT_FALSE(history[0].timestamp.empty());
  // ISO 8601 format check
  EXPECT_TRUE(history[0].timestamp.find("T") != std::string::npos);
}

TEST_F(SessionManagerTest, HistoryWithLimit) {
  session_mgr_->GetOrCreate("test:limit");

  for (int i = 0; i < 10; ++i) {
    session_mgr_->AppendMessage("test:limit", "user",
                                "msg " + std::to_string(i));
  }

  auto all = session_mgr_->GetHistory("test:limit");
  EXPECT_EQ(all.size(), 10u);

  auto last3 = session_mgr_->GetHistory("test:limit", 3);
  ASSERT_EQ(last3.size(), 3u);
  EXPECT_EQ(last3[0].content[0].text, "msg 7");
  EXPECT_EQ(last3[2].content[0].text, "msg 9");
}

TEST_F(SessionManagerTest, HistoryOfNonexistentSession) {
  auto history = session_mgr_->GetHistory("nonexistent");
  EXPECT_TRUE(history.empty());
}

// --- Usage info ---

TEST_F(SessionManagerTest, AppendMessageWithUsage) {
  session_mgr_->GetOrCreate("test:usage");

  quantclaw::UsageInfo usage;
  usage.input_tokens = 100;
  usage.output_tokens = 50;
  session_mgr_->AppendMessage("test:usage", "assistant", "Response", usage);

  auto history = session_mgr_->GetHistory("test:usage");
  ASSERT_EQ(history.size(), 1u);
  ASSERT_TRUE(history[0].usage.has_value());
  EXPECT_EQ(history[0].usage->input_tokens, 100);
  EXPECT_EQ(history[0].usage->output_tokens, 50);
}

// --- JSONL format verification ---

TEST_F(SessionManagerTest, JsonlFormatIsCorrect) {
  auto handle = session_mgr_->GetOrCreate("test:jsonl");
  session_mgr_->AppendMessage("test:jsonl", "user", "hello world");

  // Read raw JSONL file
  std::ifstream f(handle.transcript_path);
  std::string line;
  std::getline(f, line);
  f.close();

  auto j = nlohmann::json::parse(line);
  EXPECT_EQ(j["type"], "message");
  EXPECT_TRUE(j.contains("timestamp"));
  EXPECT_EQ(j["message"]["role"], "user");
  EXPECT_TRUE(j["message"]["content"].is_array());
  EXPECT_EQ(j["message"]["content"][0]["type"], "text");
  EXPECT_EQ(j["message"]["content"][0]["text"], "hello world");
}

// --- list_sessions ---

TEST_F(SessionManagerTest, ListSessions) {
  session_mgr_->GetOrCreate("agent:a:main", "Session A");
  session_mgr_->GetOrCreate("agent:b:main", "Session B");

  auto sessions = session_mgr_->ListSessions();
  EXPECT_EQ(sessions.size(), 2u);
}

TEST_F(SessionManagerTest, ListSessionsEmpty) {
  auto sessions = session_mgr_->ListSessions();
  EXPECT_TRUE(sessions.empty());
}

// --- reset_session ---

TEST_F(SessionManagerTest, ResetSession) {
  session_mgr_->GetOrCreate("test:reset");
  session_mgr_->AppendMessage("test:reset", "user", "old message");

  auto before = session_mgr_->GetHistory("test:reset");
  EXPECT_EQ(before.size(), 1u);

  session_mgr_->ResetSession("test:reset");

  auto after = session_mgr_->GetHistory("test:reset");
  EXPECT_TRUE(after.empty());
}

// --- sessions.json persistence ---

TEST_F(SessionManagerTest, PersistenceAcrossReloads) {
  session_mgr_->GetOrCreate("test:persist", "Persistent");
  session_mgr_->AppendMessage("test:persist", "user", "saved");

  // Destroy and recreate
  session_mgr_.reset();
  session_mgr_ =
      std::make_unique<quantclaw::SessionManager>(test_dir_, logger_);

  auto sessions = session_mgr_->ListSessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].session_key, "agent:main:test:persist");
  EXPECT_EQ(sessions[0].display_name, "Persistent");

  auto history = session_mgr_->GetHistory("test:persist");
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].content[0].text, "saved");
}

// --- ContentBlock ---

TEST(ContentBlockTest, MakeText) {
  auto block = quantclaw::ContentBlock::MakeText("hello");
  EXPECT_EQ(block.type, "text");
  EXPECT_EQ(block.text, "hello");

  auto j = block.ToJson();
  EXPECT_EQ(j["type"], "text");
  EXPECT_EQ(j["text"], "hello");
}

TEST(ContentBlockTest, MakeToolUse) {
  auto block =
      quantclaw::ContentBlock::MakeToolUse("t1", "read", {{"path", "/tmp"}});
  EXPECT_EQ(block.type, "tool_use");
  EXPECT_EQ(block.id, "t1");
  EXPECT_EQ(block.name, "read");

  auto j = block.ToJson();
  EXPECT_EQ(j["name"], "read");
  EXPECT_EQ(j["input"]["path"], "/tmp");
}

TEST(ContentBlockTest, MakeToolResult) {
  auto block = quantclaw::ContentBlock::MakeToolResult("t1", "file contents");
  EXPECT_EQ(block.type, "tool_result");
  EXPECT_EQ(block.tool_use_id, "t1");

  auto j = block.ToJson();
  EXPECT_EQ(j["tool_use_id"], "t1");
  EXPECT_EQ(j["content"], "file contents");
}

TEST(ContentBlockTest, Roundtrip) {
  auto original = quantclaw::ContentBlock::MakeText("roundtrip test");
  auto j = original.ToJson();
  auto parsed = quantclaw::ContentBlock::FromJson(j);

  EXPECT_EQ(parsed.type, original.type);
  EXPECT_EQ(parsed.text, original.text);
}

// --- SessionMessage ---

// --- delete_session ---

TEST_F(SessionManagerTest, DeleteSession) {
  auto handle = session_mgr_->GetOrCreate("test:delete");
  session_mgr_->AppendMessage("test:delete", "user", "hello");

  session_mgr_->DeleteSession("test:delete");

  auto sessions = session_mgr_->ListSessions();
  EXPECT_TRUE(sessions.empty());

  // JSONL file should be gone
  EXPECT_FALSE(std::filesystem::exists(handle.transcript_path));
}

TEST_F(SessionManagerTest, DeleteNonexistentSession) {
  // Should not crash
  session_mgr_->DeleteSession("nonexistent:session");
  auto sessions = session_mgr_->ListSessions();
  EXPECT_TRUE(sessions.empty());
}

// --- created_at ---

TEST_F(SessionManagerTest, CreatedAtTimestamp) {
  session_mgr_->GetOrCreate("test:created");
  auto sessions = session_mgr_->ListSessions();

  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_FALSE(sessions[0].created_at.empty());
  EXPECT_TRUE(sessions[0].created_at.find("T") != std::string::npos);
}

// --- update_display_name ---

TEST_F(SessionManagerTest, UpdateDisplayName) {
  session_mgr_->GetOrCreate("test:name", "Original");

  session_mgr_->UpdateDisplayName("test:name", "Updated Name");

  auto sessions = session_mgr_->ListSessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].display_name, "Updated Name");
}

// --- Tool call message JSONL roundtrip ---

TEST_F(SessionManagerTest, ToolCallMessageRoundtrip) {
  session_mgr_->GetOrCreate("test:toolcall");

  // Append assistant message with tool_use
  quantclaw::SessionMessage assistant_msg;
  assistant_msg.role = "assistant";
  assistant_msg.content.push_back(
      quantclaw::ContentBlock::MakeText("Let me check."));
  assistant_msg.content.push_back(quantclaw::ContentBlock::MakeToolUse(
      "t1", "read_file", {{"path", "/tmp/test"}}));
  session_mgr_->AppendMessage("test:toolcall", assistant_msg);

  // Append user message with tool_result
  quantclaw::SessionMessage result_msg;
  result_msg.role = "user";
  result_msg.content.push_back(
      quantclaw::ContentBlock::MakeToolResult("t1", "file contents here"));
  session_mgr_->AppendMessage("test:toolcall", result_msg);

  auto history = session_mgr_->GetHistory("test:toolcall");
  ASSERT_EQ(history.size(), 2u);

  // Verify assistant message
  EXPECT_EQ(history[0].role, "assistant");
  ASSERT_EQ(history[0].content.size(), 2u);
  EXPECT_EQ(history[0].content[0].type, "text");
  EXPECT_EQ(history[0].content[0].text, "Let me check.");
  EXPECT_EQ(history[0].content[1].type, "tool_use");
  EXPECT_EQ(history[0].content[1].id, "t1");
  EXPECT_EQ(history[0].content[1].name, "read_file");
  EXPECT_EQ(history[0].content[1].input["path"], "/tmp/test");

  // Verify tool_result message
  EXPECT_EQ(history[1].role, "user");
  ASSERT_EQ(history[1].content.size(), 1u);
  EXPECT_EQ(history[1].content[0].type, "tool_result");
  EXPECT_EQ(history[1].content[0].tool_use_id, "t1");
  EXPECT_EQ(history[1].content[0].content, "file contents here");
}

TEST(SessionMessageTest, JsonlRoundtrip) {
  quantclaw::SessionMessage msg;
  msg.role = "assistant";
  msg.content.push_back(quantclaw::ContentBlock::MakeText("Hello!"));
  msg.timestamp = "2026-02-23T10:00:00Z";
  msg.usage = quantclaw::UsageInfo{10, 5};

  auto j = msg.ToJsonl();
  auto parsed = quantclaw::SessionMessage::FromJsonl(j);

  EXPECT_EQ(parsed.role, "assistant");
  ASSERT_EQ(parsed.content.size(), 1u);
  EXPECT_EQ(parsed.content[0].text, "Hello!");
  EXPECT_EQ(parsed.timestamp, "2026-02-23T10:00:00Z");
  ASSERT_TRUE(parsed.usage.has_value());
  EXPECT_EQ(parsed.usage->input_tokens, 10);
  EXPECT_EQ(parsed.usage->output_tokens, 5);
}

// --- Session key normalization (OpenClaw format) ---

TEST(SessionKeyTest, ParseValidKey) {
  auto parsed = quantclaw::ParseAgentSessionKey("agent:main:main");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->agent_id, "main");
  EXPECT_EQ(parsed->rest, "main");
}

TEST(SessionKeyTest, ParseKeyWithMultipleColons) {
  auto parsed = quantclaw::ParseAgentSessionKey("agent:main:dm:user1:extra");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->agent_id, "main");
  EXPECT_EQ(parsed->rest, "dm:user1:extra");
}

TEST(SessionKeyTest, ParseInvalidKeyNoAgent) {
  EXPECT_FALSE(quantclaw::ParseAgentSessionKey("test:session").has_value());
}

TEST(SessionKeyTest, ParseInvalidKeyTooFewParts) {
  EXPECT_FALSE(quantclaw::ParseAgentSessionKey("agent:main").has_value());
}

TEST(SessionKeyTest, ParseEmptyKey) {
  EXPECT_FALSE(quantclaw::ParseAgentSessionKey("").has_value());
}

TEST(SessionKeyTest, NormalizeAlreadyValid) {
  EXPECT_EQ(quantclaw::NormalizeSessionKey("agent:main:main"),
            "agent:main:main");
}

TEST(SessionKeyTest, NormalizePlainKey) {
  EXPECT_EQ(quantclaw::NormalizeSessionKey("my-session"),
            "agent:main:my-session");
}

TEST(SessionKeyTest, NormalizeLowercases) {
  EXPECT_EQ(quantclaw::NormalizeSessionKey("agent:Main:MyChat"),
            "agent:main:mychat");
}

TEST(SessionKeyTest, NormalizeEmptyKey) {
  EXPECT_EQ(quantclaw::NormalizeSessionKey(""), "agent:main:main");
}

TEST(SessionKeyTest, NormalizeWithWhitespace) {
  EXPECT_EQ(quantclaw::NormalizeSessionKey("  agent:main:test  "),
            "agent:main:test");
}

TEST(SessionKeyTest, BuildMainSessionKey) {
  EXPECT_EQ(quantclaw::BuildMainSessionKey(), "agent:main:main");
  EXPECT_EQ(quantclaw::BuildMainSessionKey("alpha"), "agent:alpha:main");
}

TEST_F(SessionManagerTest, PlainKeyNormalizedOnCreate) {
  auto handle = session_mgr_->GetOrCreate("my-chat");
  EXPECT_EQ(handle.session_key, "agent:main:my-chat");

  // Same session retrieved with normalized key
  auto h2 = session_mgr_->GetOrCreate("my-chat");
  EXPECT_EQ(handle.session_id, h2.session_id);
}

TEST_F(SessionManagerTest, CaseInsensitiveKeys) {
  auto h1 = session_mgr_->GetOrCreate("agent:Main:MyChat");
  auto h2 = session_mgr_->GetOrCreate("agent:main:mychat");
  EXPECT_EQ(h1.session_id, h2.session_id);
}

// --- JSONL entry type: thinking_level_change ---

TEST_F(SessionManagerTest, AppendThinkingLevelChange) {
  auto handle = session_mgr_->GetOrCreate("agent:main:test-thinking");

  session_mgr_->AppendThinkingLevelChange("agent:main:test-thinking",
                                          "extended");

  // Read the JSONL file directly and verify the entry
  std::ifstream f(handle.transcript_path);
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    if (line.empty())
      continue;
    auto j = nlohmann::json::parse(line);
    if (j.value("type", "") == "thinking_level_change") {
      EXPECT_EQ(j.value("thinkingLevel", ""), "extended");
      EXPECT_FALSE(j.value("timestamp", "").empty());
      found = true;
    }
  }
  EXPECT_TRUE(found) << "thinking_level_change entry not found in transcript";
}

TEST_F(SessionManagerTest, AppendThinkingLevelChangeUnknownSession) {
  // Should log error without crashing
  EXPECT_NO_THROW(session_mgr_->AppendThinkingLevelChange(
      "agent:main:does-not-exist", "basic"));
}

// --- JSONL entry type: custom_message ---

TEST_F(SessionManagerTest, AppendCustomMessage) {
  auto handle = session_mgr_->GetOrCreate("agent:main:test-custom");

  nlohmann::json content = {{{"type", "text"}, {"text", "hello"}}};
  nlohmann::json display = {{"banner", "info"}};
  nlohmann::json details = {{"source", "system"}};

  session_mgr_->AppendCustomMessage("agent:main:test-custom", "system_alert",
                                    content, display, details);

  std::ifstream f(handle.transcript_path);
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    if (line.empty())
      continue;
    auto j = nlohmann::json::parse(line);
    if (j.value("type", "") == "custom_message") {
      EXPECT_EQ(j.value("customType", ""), "system_alert");
      EXPECT_EQ(j["content"][0]["text"].get<std::string>(), "hello");
      EXPECT_EQ(j["display"].value("banner", ""), "info");
      EXPECT_EQ(j["details"].value("source", ""), "system");
      EXPECT_FALSE(j.value("timestamp", "").empty());
      found = true;
    }
  }
  EXPECT_TRUE(found) << "custom_message entry not found in transcript";
}

TEST_F(SessionManagerTest, AppendCustomMessageDefaultArgs) {
  auto handle = session_mgr_->GetOrCreate("agent:main:test-custom-default");
  // Should not throw with default args
  EXPECT_NO_THROW(session_mgr_->AppendCustomMessage(
      "agent:main:test-custom-default", "ping"));
  std::ifstream f(handle.transcript_path);
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    if (line.empty())
      continue;
    auto j = nlohmann::json::parse(line);
    if (j.value("type", "") == "custom_message") {
      EXPECT_EQ(j.value("customType", ""), "ping");
      EXPECT_TRUE(j["content"].is_array());
      found = true;
    }
  }
  EXPECT_TRUE(found);
}
