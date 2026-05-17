// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_session.hpp"

#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/session_compaction.hpp"
#include "quantclaw/session/session_manager.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace quantclaw::gateway {

SessionHandler::SessionHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void SessionHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- sessions.list ---
  ctx_.server->RegisterHandler(
      methods::kSessionsList,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsList(params, client);
      });

  // --- sessions.history ---
  ctx_.server->RegisterHandler(
      methods::kSessionsHistory,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsHistory(params, client);
      });

  // --- sessions.delete ---
  ctx_.server->RegisterHandler(
      methods::kSessionsDelete,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsDelete(params, client);
      });

  // --- sessions.reset ---
  ctx_.server->RegisterHandler(
      methods::kSessionsReset,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsReset(params, client);
      });

  // --- sessions.patch ---
  ctx_.server->RegisterHandler(
      methods::kSessionsPatch,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsPatch(params, client);
      });

  // --- sessions.compact ---
  ctx_.server->RegisterHandler(
      methods::kSessionsCompact,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsCompact(params, client);
      });

  // --- sessions.preview (OpenClaw compat) ---
  ctx_.server->RegisterHandler(
      methods::kOcSessionsPreview,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleSessionsPreview(params, client);
      });
}

nlohmann::json SessionHandler::HandleSessionsList(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  int limit = params.value("limit", 0);
  int offset = params.value("offset", 0);

  auto sessions = ctx_.session_manager->ListSessions();
  int total = static_cast<int>(sessions.size());
  int start = std::clamp(offset, 0, total);
  int end = (limit > 0) ? std::min(start + limit, total) : total;

  // Helper: Convert ISO timestamp to milliseconds since epoch
  auto iso_to_ms = [](const std::string& iso_str) -> int64_t {
    if (iso_str.empty())
      return 0;
    std::tm tm = {};
    std::istringstream ss(iso_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail())
      return 0;
    tm.tm_isdst = 0;
#ifdef _WIN32
    auto time_t_val = _mkgmtime64(&tm);
#else
    auto time_t_val = timegm(&tm);
#endif
    if (time_t_val < 0)
      return 0;
    return static_cast<int64_t>(time_t_val) * 1000;
  };

  nlohmann::json session_rows = nlohmann::json::array();
  for (size_t i = static_cast<size_t>(start);
       i < static_cast<size_t>(end); ++i) {
    const auto& s = sessions[i];
    nlohmann::json row;
    row["key"] = s.session_key;
    row["sessionId"] = s.session_id;
    row["displayName"] = s.display_name;
    row["surface"] = s.channel.empty() ? "cli" : s.channel;
    row["updatedAt"] = iso_to_ms(s.updated_at);
    if (s.session_key.find("group:") != std::string::npos) {
      row["kind"] = "group";
    } else if (s.session_key.find("global") != std::string::npos) {
      row["kind"] = "global";
    } else {
      row["kind"] = "direct";
    }
    if (!s.spawned_by.empty())
      row["spawnedBy"] = s.spawned_by;
    if (s.spawn_depth > 0)
      row["spawnDepth"] = s.spawn_depth;
    if (!s.subagent_role.empty())
      row["subagentRole"] = s.subagent_role;
    session_rows.push_back(row);
  }

  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  return {{"ts", now_ms},
          {"path", ""},
          {"count", total},
          {"defaults",
           {{"model", ctx_.config->agent.model},
            {"contextTokens", ctx_.config->agent.context_window}}},
          {"sessions", session_rows}};
}

nlohmann::json SessionHandler::HandleSessionsHistory(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  int limit = params.value("limit", -1);

  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }

  auto history = ctx_.session_manager->GetHistory(session_key, limit);

  nlohmann::json result = nlohmann::json::array();
  for (const auto& msg : history) {
    nlohmann::json entry;
    entry["role"] = msg.role;
    entry["timestamp"] = msg.timestamp;

    nlohmann::json content_arr = nlohmann::json::array();
    for (const auto& block : msg.content) {
      content_arr.push_back(block.ToJson());
    }
    entry["content"] = content_arr;

    if (msg.usage) {
      entry["usage"] = msg.usage->ToJson();
    }

    result.push_back(entry);
  }
  return result;
}

nlohmann::json SessionHandler::HandleSessionsDelete(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("key", "");
  if (session_key.empty())
    session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("key is required");
  }
  bool deleted = ctx_.session_manager->DeleteSession(session_key);
  return {{"ok", true}, {"deleted", deleted}};
}

nlohmann::json SessionHandler::HandleSessionsReset(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }
  ctx_.session_manager->ResetSession(session_key);
  return {{"ok", true}};
}

nlohmann::json SessionHandler::HandleSessionsPatch(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("key", "");
  if (session_key.empty())
    session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("key is required");
  }

  if (params.contains("displayName")) {
    ctx_.session_manager->UpdateDisplayName(
        session_key, params["displayName"].get<std::string>());
  } else if (params.contains("label")) {
    if (!params["label"].is_null()) {
      ctx_.session_manager->UpdateDisplayName(
          session_key, params["label"].get<std::string>());
    }
  }

  return {{"ok", true},
          {"path", ""},
          {"key", session_key},
          {"entry", {{"sessionId", ""}}}};
}

nlohmann::json SessionHandler::HandleSessionsCompact(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }

  auto history = ctx_.session_manager->GetHistory(session_key);

  std::vector<nlohmann::json> history_json;
  for (const auto& m : history) {
    history_json.push_back(m.ToJsonl());
  }

  quantclaw::SessionCompaction compaction(ctx_.logger);
  quantclaw::SessionCompaction::Options opts;
  opts.max_messages = params.value("maxMessages", 100);
  opts.keep_recent = params.value("keepRecent", 20);

  if (!compaction.NeedsCompaction(history_json, opts)) {
    return {{"compacted", false}, {"reason", "below threshold"}};
  }

  auto compacted = compaction.Truncate(history_json, opts);

  return {{"compacted", true},
          {"originalCount", static_cast<int>(history.size())},
          {"newCount", static_cast<int>(compacted.size())}};
}

nlohmann::json SessionHandler::HandleSessionsPreview(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }

  auto history = ctx_.session_manager->GetHistory(session_key, 1);
  if (history.empty()) {
    return nlohmann::json::object();
  }

  const auto& msg = history.back();
  nlohmann::json entry;
  entry["role"] = msg.role;
  entry["timestamp"] = msg.timestamp;

  nlohmann::json content_arr = nlohmann::json::array();
  for (const auto& block : msg.content) {
    content_arr.push_back(block.ToJson());
  }
  entry["content"] = content_arr;

  return entry;
}

}  // namespace quantclaw::gateway
