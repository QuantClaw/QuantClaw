// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/message_commands.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

class MessageCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    reset_called_ = false;
    compact_called_ = false;
    status_called_ = false;
    last_session_key_.clear();

    MessageCommandParser::Handlers handlers;
    handlers.reset_session = [this](const std::string& key) {
      reset_called_ = true;
      last_session_key_ = key;
    };
    handlers.compact_session = [this](const std::string& key) {
      compact_called_ = true;
      last_session_key_ = key;
    };
    handlers.get_status = [this](const std::string& key) -> std::string {
      status_called_ = true;
      last_session_key_ = key;
      return "Session: " + key + "\nMessages: 42";
    };

    parser_ = std::make_unique<MessageCommandParser>(std::move(handlers));
  }

  bool reset_called_;
  bool compact_called_;
  bool status_called_;
  std::string last_session_key_;
  std::unique_ptr<MessageCommandParser> parser_;
};

// --- /new command ---

TEST_F(MessageCommandsTest, NewCommandResetsSession) {
  auto result = parser_->Parse("/new", "session:main");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(reset_called_);
  EXPECT_EQ(last_session_key_, "session:main");
  EXPECT_FALSE(result.reply.empty());
}

TEST_F(MessageCommandsTest, NewCommandCaseInsensitive) {
  auto result = parser_->Parse("/NEW", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(reset_called_);
}

// --- /reset command ---

TEST_F(MessageCommandsTest, ResetCommandResetsSession) {
  auto result = parser_->Parse("/reset", "session:test");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(reset_called_);
  EXPECT_EQ(last_session_key_, "session:test");
}

TEST_F(MessageCommandsTest, ResetWithTrailingWhitespace) {
  auto result = parser_->Parse("/reset  ", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(reset_called_);
}

// --- /compact command ---

TEST_F(MessageCommandsTest, CompactCommandCompactsSession) {
  auto result = parser_->Parse("/compact", "session:main");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(compact_called_);
  EXPECT_EQ(last_session_key_, "session:main");
}

// --- /status command ---

TEST_F(MessageCommandsTest, StatusCommandReturnsStatus) {
  auto result = parser_->Parse("/status", "session:main");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(status_called_);
  EXPECT_NE(result.reply.find("Session: session:main"), std::string::npos);
  EXPECT_NE(result.reply.find("42"), std::string::npos);
}

// --- /help command ---

TEST_F(MessageCommandsTest, HelpCommandListsCommands) {
  auto result = parser_->Parse("/help", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_NE(result.reply.find("/new"), std::string::npos);
  EXPECT_NE(result.reply.find("/reset"), std::string::npos);
  EXPECT_NE(result.reply.find("/compact"), std::string::npos);
  EXPECT_NE(result.reply.find("/status"), std::string::npos);
}

// --- /commands command ---

TEST_F(MessageCommandsTest, CommandsCommandListsCommands) {
  auto result = parser_->Parse("/commands", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_NE(result.reply.find("/help"), std::string::npos);
}

// --- Non-command messages ---

TEST_F(MessageCommandsTest, RegularMessageNotHandled) {
  auto result = parser_->Parse("Hello, how are you?", "s1");
  EXPECT_FALSE(result.handled);
  EXPECT_FALSE(reset_called_);
  EXPECT_FALSE(compact_called_);
}

TEST_F(MessageCommandsTest, EmptyMessageNotHandled) {
  auto result = parser_->Parse("", "s1");
  EXPECT_FALSE(result.handled);
}

TEST_F(MessageCommandsTest, SlashInMiddleNotHandled) {
  auto result = parser_->Parse("What does /new mean?", "s1");
  EXPECT_FALSE(result.handled);
}

TEST_F(MessageCommandsTest, UnknownCommandNotHandled) {
  auto result = parser_->Parse("/unknown_command", "s1");
  EXPECT_FALSE(result.handled);
}

TEST_F(MessageCommandsTest, LeadingWhitespaceBeforeSlash) {
  auto result = parser_->Parse("  /reset", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(reset_called_);
}

// --- ListCommands ---

TEST_F(MessageCommandsTest, ListCommandsReturnsExpectedSet) {
  auto cmds = MessageCommandParser::ListCommands();
  EXPECT_GE(cmds.size(), 5u);

  bool has_new = false, has_reset = false, has_compact = false;
  for (const auto& [name, desc] : cmds) {
    if (name == "/new")
      has_new = true;
    if (name == "/reset")
      has_reset = true;
    if (name == "/compact")
      has_compact = true;
    EXPECT_FALSE(desc.empty());
  }
  EXPECT_TRUE(has_new);
  EXPECT_TRUE(has_reset);
  EXPECT_TRUE(has_compact);
}

// --- Null handlers ---

TEST_F(MessageCommandsTest, NullResetHandlerDoesNotCrash) {
  MessageCommandParser::Handlers empty_handlers;
  MessageCommandParser parser(std::move(empty_handlers));

  auto result = parser.Parse("/reset", "s1");
  EXPECT_TRUE(result.handled);
}

TEST_F(MessageCommandsTest, NullCompactHandlerDoesNotCrash) {
  MessageCommandParser::Handlers empty_handlers;
  MessageCommandParser parser(std::move(empty_handlers));

  auto result = parser.Parse("/compact", "s1");
  EXPECT_TRUE(result.handled);
}

TEST_F(MessageCommandsTest, NullStatusHandlerReturnsFallback) {
  MessageCommandParser::Handlers empty_handlers;
  MessageCommandParser parser(std::move(empty_handlers));

  auto result = parser.Parse("/status", "s1");
  EXPECT_TRUE(result.handled);
  EXPECT_NE(result.reply.find("s1"), std::string::npos);
}
