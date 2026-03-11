// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/session/session_manager.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace quantclaw {

// --- Session Key Utilities ---

static std::string to_lower(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key) {
  if (key.empty())
    return std::nullopt;

  // Split by ':'
  std::vector<std::string> parts;
  std::string::size_type start = 0;
  while (start < key.size()) {
    auto pos = key.find(':', start);
    if (pos == std::string::npos) {
      parts.push_back(key.substr(start));
      break;
    }
    parts.push_back(key.substr(start, pos - start));
    start = pos + 1;
  }

  // Need at least 3 parts: "agent", agentId, rest
  if (parts.size() < 3)
    return std::nullopt;
  if (parts[0] != "agent")
    return std::nullopt;

  std::string agent_id = parts[1];
  if (agent_id.empty())
    return std::nullopt;

  // rest = everything after the second colon
  std::string rest;
  for (size_t i = 2; i < parts.size(); ++i) {
    if (i > 2)
      rest += ':';
    rest += parts[i];
  }
  if (rest.empty())
    return std::nullopt;

  return ParsedSessionKey{agent_id, rest};
}

std::string NormalizeSessionKey(const std::string& key,
                                const std::string& default_agent_id) {
  std::string trimmed = key;
  // Trim whitespace
  while (!trimmed.empty() &&
         std::isspace(static_cast<unsigned char>(trimmed.front())))
    trimmed.erase(trimmed.begin());
  while (!trimmed.empty() &&
         std::isspace(static_cast<unsigned char>(trimmed.back())))
    trimmed.pop_back();

  if (trimmed.empty()) {
    return "agent:" + to_lower(default_agent_id) + ":main";
  }

  // Already in correct format?
  auto parsed = ParseAgentSessionKey(trimmed);
  if (parsed) {
    return "agent:" + to_lower(parsed->agent_id) + ":" + to_lower(parsed->rest);
  }

  // Not in agent:x:y format — wrap it
  return "agent:" + to_lower(default_agent_id) + ":" + to_lower(trimmed);
}

std::string BuildMainSessionKey(const std::string& agent_id) {
  return "agent:" + to_lower(agent_id) + ":main";
}

// --- ContentBlock ---

nlohmann::json ContentBlock::ToJson() const {
  nlohmann::json j;
  j["type"] = type;
  if (type == "text" || type == "thinking") {
    j["text"] = text;
  } else if (type == "tool_use") {
    j["id"] = id;
    j["name"] = name;
    j["input"] = input;
  } else if (type == "tool_result") {
    j["tool_use_id"] = tool_use_id;
    j["content"] = content;
  }
  return j;
}

ContentBlock ContentBlock::FromJson(const nlohmann::json& j) {
  ContentBlock cb;
  cb.type = j.value("type", "text");
  if (cb.type == "text" || cb.type == "thinking") {
    cb.text = j.value("text", "");
  } else if (cb.type == "tool_use") {
    cb.id = j.value("id", "");
    cb.name = j.value("name", "");
    cb.input = j.value("input", nlohmann::json::object());
  } else if (cb.type == "tool_result") {
    cb.tool_use_id = j.value("tool_use_id", "");
    cb.content = j.value("content", "");
  }
  return cb;
}

ContentBlock ContentBlock::MakeText(const std::string& text) {
  ContentBlock cb;
  cb.type = "text";
  cb.text = text;
  return cb;
}

ContentBlock ContentBlock::MakeToolUse(const std::string& id,
                                       const std::string& name,
                                       const nlohmann::json& input) {
  ContentBlock cb;
  cb.type = "tool_use";
  cb.id = id;
  cb.name = name;
  cb.input = input;
  return cb;
}

ContentBlock ContentBlock::MakeToolResult(const std::string& tool_use_id,
                                          const std::string& content) {
  ContentBlock cb;
  cb.type = "tool_result";
  cb.tool_use_id = tool_use_id;
  cb.content = content;
  return cb;
}

// --- SessionMessage ---

nlohmann::json SessionMessage::ToJsonl() const {
  nlohmann::json j;
  j["type"] = "message";
  j["timestamp"] = timestamp;

  nlohmann::json msg;
  msg["role"] = role;

  nlohmann::json content_arr = nlohmann::json::array();
  for (const auto& block : content) {
    content_arr.push_back(block.ToJson());
  }
  msg["content"] = content_arr;

  if (usage) {
    msg["usage"] = usage->ToJson();
  }

  j["message"] = msg;
  return j;
}

SessionMessage SessionMessage::FromJsonl(const nlohmann::json& j) {
  SessionMessage msg;
  msg.timestamp = j.value("timestamp", "");

  if (j.contains("message")) {
    const auto& m = j["message"];
    msg.role = m.value("role", "");

    if (m.contains("content")) {
      if (m["content"].is_array()) {
        for (const auto& block : m["content"]) {
          msg.content.push_back(ContentBlock::FromJson(block));
        }
      } else if (m["content"].is_string()) {
        // Legacy: plain string content
        msg.content.push_back(
            ContentBlock::MakeText(m["content"].get<std::string>()));
      }
    }

    if (m.contains("usage")) {
      msg.usage = UsageInfo::FromJson(m["usage"]);
    }
  }

  return msg;
}

// --- SessionManager ---

SessionManager::SessionManager(const std::filesystem::path& sessions_dir,
                               std::shared_ptr<spdlog::logger> logger)
    : sessions_dir_(sessions_dir), logger_(logger) {
  std::filesystem::create_directories(sessions_dir_);
  LoadStore();
  logger_->info("SessionManager initialized at: {}", sessions_dir_.string());
}

SessionHandle SessionManager::GetOrCreate(const std::string& session_key,
                                          const std::string& display_name,
                                          const std::string& channel) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // Normalize session key to OpenClaw format: agent:<agentId>:<rest>
  std::string normalized = NormalizeSessionKey(session_key);

  auto it = store_.find(normalized);
  if (it != store_.end()) {
    return {normalized, it->second.session_id,
            transcript_path(it->second.session_id)};
  }

  // Create new session
  std::string sid = generate_session_id();
  std::string now = get_timestamp();
  SessionInfo info;
  info.session_key = normalized;
  info.session_id = sid;
  info.updated_at = now;
  info.created_at = now;
  info.display_name = display_name.empty() ? normalized : display_name;
  info.channel = channel;

  store_[normalized] = info;
  SaveStore();

  logger_->info("Created new session: {} -> {}", normalized, sid);

  return {normalized, sid, transcript_path(sid)};
}

void SessionManager::AppendMessage(const std::string& session_key,
                                   const std::string& role,
                                   const std::string& text_content,
                                   const std::optional<UsageInfo>& usage) {
  SessionMessage msg;
  msg.role = role;
  msg.content.push_back(ContentBlock::MakeText(text_content));
  msg.timestamp = get_timestamp();
  msg.usage = usage;

  AppendMessage(NormalizeSessionKey(session_key), msg);
}

void SessionManager::AppendMessage(const std::string& session_key,
                                   const SessionMessage& msg) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    logger_->error("Session not found: {}", session_key);
    return;
  }

  auto path = transcript_path(it->second.session_id);
  std::ofstream file(path, std::ios::app);
  if (!file.is_open()) {
    logger_->error("Failed to open transcript: {}", path.string());
    return;
  }

  file << msg.ToJsonl().dump() << "\n";
  file.close();

  // Update timestamp
  it->second.updated_at = get_timestamp();
  SaveStore();
}

bool SessionManager::AppendTranscriptEntry(const std::string& session_key,
                                           const nlohmann::json& entry) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    logger_->error("Session not found: {}", session_key);
    return false;
  }
  auto path = transcript_path(it->second.session_id);
  std::ofstream file(path, std::ios::app);
  if (!file.is_open()) {
    logger_->error("Failed to open transcript: {}", path.string());
    return false;
  }
  file << entry.dump() << "\n";
  file.close();
  it->second.updated_at = get_timestamp();
  SaveStore();
  return true;
}

void SessionManager::AppendThinkingLevelChange(
    const std::string& session_key, const std::string& thinking_level) {
  nlohmann::json entry = {{"type", "thinking_level_change"},
                          {"timestamp", get_timestamp()},
                          {"thinkingLevel", thinking_level}};
  AppendTranscriptEntry(session_key, entry);
}

void SessionManager::AppendCustomMessage(const std::string& session_key,
                                         const std::string& custom_type,
                                         const nlohmann::json& content,
                                         const nlohmann::json& display,
                                         const nlohmann::json& details) {
  nlohmann::json entry = {
      {"type", "custom_message"},  {"timestamp", get_timestamp()},
      {"customType", custom_type}, {"content", content},
      {"display", display},        {"details", details}};
  AppendTranscriptEntry(session_key, entry);
}

std::vector<SessionMessage>
SessionManager::GetHistory(const std::string& session_key,
                           int max_messages) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<SessionMessage> messages;

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return messages;
  }

  auto path = transcript_path(it->second.session_id);
  if (!std::filesystem::exists(path)) {
    return messages;
  }

  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;
    try {
      auto j = nlohmann::json::parse(line);
      if (!j.contains("type") || !j["type"].is_string() ||
          j["type"].get<std::string>() != "message")
        continue;
      messages.push_back(SessionMessage::FromJsonl(j));
    } catch (const std::exception& e) {
      logger_->warn("Failed to parse JSONL line: {}", e.what());
    }
  }

  if (max_messages > 0 && static_cast<int>(messages.size()) > max_messages) {
    messages.erase(messages.begin(),
                   messages.begin() + (messages.size() - max_messages));
  }

  return messages;
}

std::vector<SessionInfo> SessionManager::ListSessions() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<SessionInfo> result;
  result.reserve(store_.size());
  for (const auto& [key, info] : store_) {
    result.push_back(info);
  }
  // Sort by updated_at descending
  std::sort(result.begin(), result.end(),
            [](const SessionInfo& a, const SessionInfo& b) {
              return a.updated_at > b.updated_at;
            });
  return result;
}

void SessionManager::DeleteSession(const std::string& session_key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    logger_->warn("Cannot delete non-existent session: {}", normalized);
    return;
  }

  // Remove transcript file
  auto path = transcript_path(it->second.session_id);
  if (std::filesystem::exists(path)) {
    std::filesystem::remove(path);
  }

  store_.erase(it);
  SaveStore();

  logger_->info("Deleted session: {}", normalized);
}

void SessionManager::UpdateDisplayName(const std::string& session_key,
                                       const std::string& name) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return;
  }

  it->second.display_name = name;
  SaveStore();
}

void SessionManager::ResetSession(const std::string& session_key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    logger_->warn("Cannot reset non-existent session: {}", session_key);
    return;
  }

  // Archive old transcript
  auto old_path = transcript_path(it->second.session_id);
  if (std::filesystem::exists(old_path)) {
    auto archive_path = old_path;
    archive_path += ".reset." + get_timestamp();
    std::filesystem::rename(old_path, archive_path);
    logger_->info("Archived transcript: {}", archive_path.string());
  }

  // Create new session ID
  std::string new_sid = generate_session_id();
  it->second.session_id = new_sid;
  it->second.updated_at = get_timestamp();
  SaveStore();

  logger_->info("Reset session: {} -> {}", normalized, new_sid);
}

void SessionManager::SaveStore() {
  auto store_path = sessions_dir_ / "sessions.json";
  nlohmann::json j = nlohmann::json::object();
  for (const auto& [key, info] : store_) {
    j[key] = {{"sessionId", info.session_id},
              {"updatedAt", info.updated_at},
              {"createdAt", info.created_at},
              {"displayName", info.display_name},
              {"channel", info.channel}};
  }
  std::ofstream file(store_path);
  if (file.is_open()) {
    file << j.dump(2);
    file.close();
  }
}

void SessionManager::LoadStore() {
  auto store_path = sessions_dir_ / "sessions.json";
  if (!std::filesystem::exists(store_path)) {
    return;
  }

  try {
    std::ifstream file(store_path);
    nlohmann::json j;
    file >> j;
    file.close();

    for (const auto& [key, value] : j.items()) {
      SessionInfo info;
      info.session_key = key;
      info.session_id = value.value("sessionId", "");
      info.updated_at = value.value("updatedAt", "");
      info.created_at = value.value("createdAt", "");
      info.display_name = value.value("displayName", "");
      info.channel = value.value("channel", "cli");
      store_[key] = info;
    }

    logger_->info("Loaded {} sessions from store", store_.size());
  } catch (const std::exception& e) {
    logger_->warn("Failed to load sessions.json: {}", e.what());
  }
}

std::string SessionManager::generate_session_id() const {
  thread_local static std::random_device rd;
  thread_local static std::mt19937 gen(rd());
  thread_local static std::uniform_int_distribution<> dis(0, 15);
  static const char* hex = "0123456789abcdef";

  std::string id;
  id.reserve(12);
  for (int i = 0; i < 12; ++i) {
    id += hex[dis(gen)];
  }
  return id;
}

std::string SessionManager::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

std::filesystem::path
SessionManager::transcript_path(const std::string& session_id) const {
  return sessions_dir_ / (session_id + ".jsonl");
}

}  // namespace quantclaw
