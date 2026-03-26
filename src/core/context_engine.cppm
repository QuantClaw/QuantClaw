// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.core.context_engine;

import std;

export namespace quantclaw {

struct Message;

struct AssembleResult {
  std::vector<Message> messages;
  int estimated_tokens = 0;
};

class ContextEngine {
 public:
  virtual ~ContextEngine() = default;

  virtual std::string Name() const = 0;

  virtual void Bootstrap(const std::string& session_key) {
    (void)session_key;
  }

  virtual AssembleResult Assemble(const std::vector<Message>& history,
                                  const std::string& system_prompt,
                                  const std::string& user_message,
                                  int context_window, int max_tokens) = 0;

  virtual std::vector<Message>
  CompactOverflow(const std::vector<Message>& messages,
                  const std::string& system_prompt, int keep_recent) = 0;

  virtual void AfterTurn(const std::vector<Message>& new_messages,
                         const std::string& session_key) {
    (void)new_messages;
    (void)session_key;
  }

  virtual void OnSubagentSpawn(const std::string& parent_key,
                               const std::string& child_key) {
    (void)parent_key;
    (void)child_key;
  }

  virtual void OnSubagentEnded(const std::string& child_key) {
    (void)child_key;
  }
};

}  // namespace quantclaw
