// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/message_commands.hpp"

#include <algorithm>
#include <sstream>

namespace quantclaw {

MessageCommandParser::MessageCommandParser(Handlers handlers)
    : handlers_(std::move(handlers)) {}

CommandResult
MessageCommandParser::Parse(const std::string& message,
                            const std::string& session_key) const {
  // Trim leading whitespace
  auto start = message.find_first_not_of(" \t\r\n");
  if (start == std::string::npos || message[start] != '/') {
    return {false, ""};
  }

  // Extract command name (everything after '/' until whitespace)
  auto cmd_start = start + 1;
  auto cmd_end = message.find_first_of(" \t\r\n", cmd_start);
  std::string cmd = message.substr(cmd_start, cmd_end == std::string::npos
                                                  ? std::string::npos
                                                  : cmd_end - cmd_start);

  // Normalize to lowercase
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (cmd == "new" || cmd == "reset") {
    if (handlers_.reset_session) {
      handlers_.reset_session(session_key);
    }
    return {true, "Session reset. Starting fresh conversation."};
  }

  if (cmd == "compact") {
    if (handlers_.compact_session) {
      handlers_.compact_session(session_key);
    }
    return {true, "Session history compacted."};
  }

  if (cmd == "status") {
    std::string status;
    if (handlers_.get_status) {
      status = handlers_.get_status(session_key);
    } else {
      status = "Session: " + session_key;
    }
    return {true, status};
  }

  if (cmd == "help" || cmd == "commands") {
    std::ostringstream oss;
    oss << "Available commands:\n";
    for (const auto& [name, desc] : ListCommands()) {
      oss << "  " << name << " — " << desc << "\n";
    }
    return {true, oss.str()};
  }

  // Unknown command — don't intercept, let it pass through to the LLM
  return {false, ""};
}

std::vector<std::pair<std::string, std::string>>
MessageCommandParser::ListCommands() {
  return {
      {"/new", "Start a new session (reset history)"},
      {"/reset", "Reset the current session"},
      {"/compact", "Compact session history to reduce context size"},
      {"/status", "Show current session status"},
      {"/help", "Show available commands"},
      {"/commands", "List all available commands"},
  };
}

}  // namespace quantclaw
