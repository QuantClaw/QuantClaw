// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quantclaw {

// Result of handling a message command.
// If a command was matched, `handled` is true and `reply` contains
// the text response to send back (bypassing the LLM).
struct CommandResult {
  bool handled = false;
  std::string reply;
};

// Parses and handles in-conversation slash commands like /new, /reset,
// /compact, /help, /status, /commands.
//
// If the user message starts with a recognized command, we execute it
// and return a CommandResult with handled=true (the message is NOT
// forwarded to the LLM). Otherwise, handled=false and the caller
// should proceed normally.
class MessageCommandParser {
 public:
  // Callbacks for command execution.
  // These are injected so the parser doesn't depend on concrete managers.
  struct Handlers {
    std::function<void(const std::string& session_key)> reset_session;
    std::function<void(const std::string& session_key)> compact_session;
    std::function<std::string(const std::string& session_key)> get_status;
  };

  explicit MessageCommandParser(Handlers handlers);

  // Parse and execute a message command.
  // `message` is the raw user text. `session_key` is the current session.
  // Returns a CommandResult. If handled=true, the caller should send
  // result.reply back and NOT forward the message to the LLM.
  CommandResult Parse(const std::string& message,
                      const std::string& session_key) const;

  // Returns the list of recognized commands with descriptions.
  static std::vector<std::pair<std::string, std::string>> ListCommands();

 private:
  Handlers handlers_;
};

}  // namespace quantclaw
