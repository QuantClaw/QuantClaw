// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.session.session_manager;

import std;
import nlohmann.json;
import <spdlog/spdlog.h>;

import quantclaw.common.noncopyable;
import quantclaw.core.content_block;

export namespace quantclaw {

struct ParsedSessionKey {
  std::string agent_id;
  std::string rest;
};

std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key);
std::string NormalizeSessionKey(const std::string& key,
                                const std::string& default_agent_id = "main");
std::string BuildMainSessionKey(const std::string& agent_id = "main");

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

struct SessionMessage {
  std::string role;
  std::vector<ContentBlock> content;
  std::string timestamp;
  std::optional<UsageInfo> usage;

  nlohmann::json ToJsonl() const;
  static SessionMessage FromJsonl(const nlohmann::json& j);
};

struct SessionInfo {
  std::string session_key;
  std::string session_id;
  std::string updated_at;
  std::string created_at;
  std::string display_name;
  std::string channel;
  std::string spawned_by;
  int spawn_depth = 0;
  std::string subagent_role;
};

struct SessionCreateOptions {
  std::string display_name;
  std::string channel = "cli";
  std::string spawned_by;
  int spawn_depth = 0;
  std::string subagent_role;
};

struct SessionHandle {
  std::string session_key;
  std::string session_id;
  std::filesystem::path transcript_path;
};

class SessionManager : public Noncopyable {
 public:
  SessionManager(const std::filesystem::path& sessions_dir,
                 std::shared_ptr<spdlog::logger> logger);

  SessionHandle GetOrCreate(const std::string& session_key,
                            const std::string& display_name = "",
                            const std::string& channel = "cli");
  SessionHandle GetOrCreate(const std::string& session_key,
                            const SessionCreateOptions& opts);

  void AppendMessage(const std::string& session_key, const std::string& role,
                     const std::string& text_content,
                     const std::optional<UsageInfo>& usage = std::nullopt);
  void AppendMessage(const std::string& session_key, const SessionMessage& msg);
  void AppendThinkingLevelChange(const std::string& session_key,
                                 const std::string& thinking_level);
  void AppendCustomMessage(const std::string& session_key,
                      const std::string& custom_type,
                      const nlohmann::json& content = nlohmann::json::array(),
                      const nlohmann::json& display = nlohmann::json::object(),
                      const nlohmann::json& details = nlohmann::json::object());

  std::vector<SessionMessage> GetHistory(const std::string& session_key,
                                         int max_messages = -1) const;
  std::vector<SessionInfo> ListSessions() const;
  bool DeleteSession(const std::string& session_key);
  void ResetSession(const std::string& session_key);
  void UpdateDisplayName(const std::string& session_key,
                         const std::string& name);
  void SaveStore();
  void LoadStore();

 private:
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, SessionInfo> store_;

  std::string generate_session_id() const;
  std::string get_timestamp() const;
  std::filesystem::path transcript_path(const std::string& session_id) const;
  bool AppendTranscriptEntry(const std::string& session_key,
                             const nlohmann::json& entry);
};

}  // namespace quantclaw
