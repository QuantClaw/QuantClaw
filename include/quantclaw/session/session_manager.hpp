// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/common/noncopyable.hpp"
#include "quantclaw/core/content_block.hpp"

namespace quantclaw {

// --- Session Key Utilities (OpenClaw compatible) ---

struct ParsedSessionKey {
  std::string agent_id;
  std::string rest;
};

// Parse a session key into agent_id and rest.
// Returns nullopt if the key doesn't match the "agent:<agentId>:<rest>" format.
std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key);

// Normalize a session key to OpenClaw format.
// - Plain keys like "my-session" → "agent:main:my-session"
// - Keys missing the "agent:" prefix get it added
// - Agent ID and rest are lowercased
std::string NormalizeSessionKey(const std::string& key,
                                const std::string& default_agent_id = "main");

// Build the default main session key: "agent:<agentId>:main"
std::string BuildMainSessionKey(const std::string& agent_id = "main");

// --- Usage Info ---

struct UsageInfo {
  int input_tokens = 0;
  int output_tokens = 0;

  nlohmann::json ToJson() const {
    return {{"inputTokens", input_tokens}, {"outputTokens", output_tokens}};
  }
  static UsageInfo FromJson(const nlohmann::json& j) {
    UsageInfo u;
    u.input_tokens = j.value("inputTokens", 0);
    u.output_tokens = j.value("outputTokens", 0);
    return u;
  }
};

// --- Session Message (JSONL line format) ---

struct SessionMessage {
  std::string role;  // "user" | "assistant" | "system" | "tool"
  std::vector<ContentBlock> content;
  std::string timestamp;  // ISO 8601
  std::optional<UsageInfo> usage;

  nlohmann::json ToJsonl() const;
  static SessionMessage FromJsonl(const nlohmann::json& j);
};

// --- Session Info (sessions.json entry) ---

struct SessionInfo {
  std::string session_key;
  std::string session_id;
  std::string updated_at;
  std::string created_at;
  std::string display_name;
  std::string channel;
};

// --- Session Handle ---

struct SessionHandle {
  std::string session_key;
  std::string session_id;
  std::filesystem::path transcript_path;
};

// --- Session Manager ---

class SessionManager : public Noncopyable {
 public:
  SessionManager(const std::filesystem::path& sessions_dir,
                 std::shared_ptr<spdlog::logger> logger);

  // Get or create a session by key
  SessionHandle GetOrCreate(const std::string& session_key,
                            const std::string& display_name = "",
                            const std::string& channel = "cli");

  // Append a message to the session transcript
  void AppendMessage(const std::string& session_key, const std::string& role,
                     const std::string& text_content,
                     const std::optional<UsageInfo>& usage = std::nullopt);

  // Append a full SessionMessage
  void AppendMessage(const std::string& session_key, const SessionMessage& msg);

  // Append a thinking_level_change entry (OpenClaw JSONL compat)
  void AppendThinkingLevelChange(const std::string& session_key,
                                 const std::string& thinking_level);

  // Append a custom_message entry (OpenClaw JSONL compat)
  // custom_type: arbitrary string identifier; content/display/details are
  // optional JSON
  void
  AppendCustomMessage(const std::string& session_key,
                      const std::string& custom_type,
                      const nlohmann::json& content = nlohmann::json::array(),
                      const nlohmann::json& display = nlohmann::json::object(),
                      const nlohmann::json& details = nlohmann::json::object());

  // Get session history
  std::vector<SessionMessage> GetHistory(const std::string& session_key,
                                         int max_messages = -1) const;

  // List all sessions
  std::vector<SessionInfo> ListSessions() const;

  // Delete a session entirely
  void DeleteSession(const std::string& session_key);

  // Reset a session (archive old, create new session ID)
  void ResetSession(const std::string& session_key);

  // Update display name
  void UpdateDisplayName(const std::string& session_key,
                         const std::string& name);

  // Persistence
  void SaveStore();
  void LoadStore();

 private:
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::shared_mutex mutex_;  // shared for reads, exclusive for writes

  // session_key -> SessionInfo
  std::unordered_map<std::string, SessionInfo> store_;

  std::string generate_session_id() const;
  std::string get_timestamp() const;
  std::filesystem::path transcript_path(const std::string& session_id) const;

  // Shared boilerplate: normalize key, look up session, open transcript,
  // write entry, update updated_at, and SaveStore. Returns true on success.
  // Caller must NOT hold mutex_ when calling this.
  bool AppendTranscriptEntry(const std::string& session_key,
                             const nlohmann::json& entry);
};

}  // namespace quantclaw
