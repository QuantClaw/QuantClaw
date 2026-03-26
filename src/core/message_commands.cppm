// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.message_commands;

import std;

export namespace quantclaw {

struct CommandResult {
  bool handled = false;
  std::string reply;
};

class MessageCommandParser {
 public:
  struct Handlers {
    std::function<void(const std::string& session_key)> reset_session;
    std::function<void(const std::string& session_key)> compact_session;
    std::function<std::string(const std::string& session_key)> get_status;
  };

  explicit MessageCommandParser(Handlers handlers);
  CommandResult Parse(const std::string& message,
                      const std::string& session_key) const;
  static std::vector<std::pair<std::string, std::string>> ListCommands();

 private:
  Handlers handlers_;
};

}  // namespace quantclaw