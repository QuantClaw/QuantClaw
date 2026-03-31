// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.gateway.protocol;

import std;
import nlohmann.json;

export namespace quantclaw::gateway {

enum class FrameType { kRequest, kResponse, kEvent };

inline std::string FrameTypeToString(FrameType type) {
  switch (type) {
    case FrameType::kRequest:
      return "req";
    case FrameType::kResponse:
      return "res";
    case FrameType::kEvent:
      return "event";
  }
  return "unknown";
}

inline FrameType FrameTypeFromString(const std::string& str) {
  if (str == "req")
    return FrameType::kRequest;
  if (str == "res")
    return FrameType::kResponse;
  if (str == "event")
    return FrameType::kEvent;
  throw std::runtime_error("Unknown frame type: " + str);
}

inline FrameType ParseFrameType(const nlohmann::json& j) {
  return FrameTypeFromString(j.value("type", ""));
}

namespace methods {
inline constexpr const char* kGatewayHealth = "gateway.health";
inline constexpr const char* kGatewayStatus = "gateway.status";
inline constexpr const char* kConfigGet = "config.get";
inline constexpr const char* kConfigSet = "config.set";
inline constexpr const char* kConfigReload = "config.reload";
inline constexpr const char* kAgentRequest = "agent.request";
inline constexpr const char* kAgentStop = "agent.stop";
inline constexpr const char* kSessionsList = "sessions.list";
inline constexpr const char* kSessionsHistory = "sessions.history";
inline constexpr const char* kSessionsDelete = "sessions.delete";
inline constexpr const char* kSessionsReset = "sessions.reset";
inline constexpr const char* kSessionsPatch = "sessions.patch";
inline constexpr const char* kSessionsCompact = "sessions.compact";
inline constexpr const char* kOcConnect = "connect";
inline constexpr const char* kConnectHello = "connect.hello";
inline constexpr const char* kChannelsList = "channels.list";
inline constexpr const char* kChannelsStatus = "channels.status";
inline constexpr const char* kChainExecute = "chain.execute";
inline constexpr const char* kOcChatSend = "chat.send";
inline constexpr const char* kOcChatHistory = "chat.history";
inline constexpr const char* kOcChatAbort = "chat.abort";
inline constexpr const char* kOcHealth = "health";
inline constexpr const char* kOcStatus = "status";
inline constexpr const char* kOcModelsList = "models.list";
inline constexpr const char* kOcToolsCatalog = "tools.catalog";
inline constexpr const char* kOcSessionsPreview = "sessions.preview";
inline constexpr const char* kSkillsStatus = "skills.status";
inline constexpr const char* kSkillsInstall = "skills.install";
inline constexpr const char* kCronList = "cron.list";
inline constexpr const char* kCronAdd = "cron.add";
inline constexpr const char* kCronRemove = "cron.remove";
inline constexpr const char* kCronUpdate = "cron.update";
inline constexpr const char* kCronRun = "cron.run";
inline constexpr const char* kCronRuns = "cron.runs";
inline constexpr const char* kExecApprovalReq = "exec.approval.request";
inline constexpr const char* kExecApprovals = "exec.approvals";
inline constexpr const char* kModelsSet = "models.set";
inline constexpr const char* kPluginsList = "plugins.list";
inline constexpr const char* kPluginsTools = "plugins.tools";
inline constexpr const char* kPluginsCallTool = "plugins.callTool";
inline constexpr const char* kPluginsServices = "plugins.services";
inline constexpr const char* kPluginsProviders = "plugins.providers";
inline constexpr const char* kPluginsCommands = "plugins.commands";
inline constexpr const char* kPluginsGateway = "plugins.gateway";
inline constexpr const char* kQueueStatus = "queue.status";
inline constexpr const char* kQueueConfigure = "queue.configure";
inline constexpr const char* kQueueCancel = "queue.cancel";
inline constexpr const char* kQueueAbort = "queue.abort";
inline constexpr const char* kMemoryStatus = "memory.status";
inline constexpr const char* kMemorySearch = "memory.search";
}

namespace events {
inline constexpr const char* kConnectChallenge = "connect.challenge";
inline constexpr const char* kTextDelta = "text.delta";
inline constexpr const char* kToolUse = "tool.use";
inline constexpr const char* kToolResult = "tool.result";
inline constexpr const char* kMessageEnd = "message.end";
inline constexpr const char* kOcChat = "chat";
inline constexpr const char* kOcAgent = "agent";
}

struct RpcRequest {
  std::string id;
  std::string method;
  nlohmann::json params;

  nlohmann::json ToJson() const {
    return {{"type", "req"}, {"id", id}, {"method", method},
            {"params", params}};
  }

  static RpcRequest FromJson(const nlohmann::json& j) {
    RpcRequest req;
    req.id = j.at("id").get<std::string>();
    req.method = j.at("method").get<std::string>();
    auto it = j.find("params");
    req.params = (it != j.end() && !it->is_null()) ? *it : nlohmann::json::object();
    return req;
  }
};

struct RpcError {
  std::string code = "INTERNAL_ERROR";
  std::string message;
  bool retryable = false;
  int retry_after_ms = 0;

  nlohmann::json ToJson() const {
    nlohmann::json j = {
        {"code", code}, {"message", message}, {"retryable", retryable}};
    if (retry_after_ms > 0) {
      j["retryAfterMs"] = retry_after_ms;
    }
    return j;
  }
};

struct RpcResponse {
  std::string id;
  bool ok = true;
  nlohmann::json payload;
  RpcError error;

  nlohmann::json ToJson() const {
    nlohmann::json j = {{"type", "res"}, {"id", id}, {"ok", ok}};
    if (ok) {
      j["payload"] = payload;
    } else {
      j["error"] = error.ToJson();
    }
    return j;
  }

  static RpcResponse success(const std::string& id,
                             const nlohmann::json& payload) {
    return {id, true, payload, {}};
  }

  static RpcResponse failure(const std::string& id,
                             const std::string& message,
                             const std::string& code = "INTERNAL_ERROR",
                             bool retryable = false, int retry_after_ms = 0) {
    return {id, false, {}, {code, message, retryable, retry_after_ms}};
  }
};

struct RpcEvent {
  std::string event;
  nlohmann::json payload;
  std::optional<std::uint64_t> seq;
  std::optional<std::uint64_t> state_version;

  nlohmann::json ToJson() const {
    nlohmann::json j = {{"type", "event"}, {"event", event}, {"payload", payload}};
    if (seq)
      j["seq"] = *seq;
    if (state_version)
      j["stateVersion"] = *state_version;
    return j;
  }
};

struct ConnectChallenge {
  std::string nonce;
  std::int64_t timestamp;

  nlohmann::json ToJson() const {
    return {{"type", "event"},
            {"event", "connect.challenge"},
            {"payload", {{"nonce", nonce}, {"ts", timestamp}}}};
  }
};

struct ConnectHelloParams {
  int min_protocol = 1;
  int max_protocol = 3;
  std::string client_name;
  std::string client_version;
  std::string role;
  std::vector<std::string> scopes;
  std::string auth_token;
  std::string device_id;

  static ConnectHelloParams FromJson(const nlohmann::json& j) {
    ConnectHelloParams p;
    p.min_protocol = j.value("minProtocol", 1);
    p.max_protocol = j.value("maxProtocol", 3);
    p.role = j.value("role", "operator");
    p.scopes = j.value("scopes", std::vector<std::string>{"operator.read", "operator.write"});

    if (j.contains("client") && j["client"].is_object()) {
      p.client_name = j["client"].value("name", "");
      p.client_version = j["client"].value("version", "");
    } else {
      p.client_name = j.value("clientName", "");
      p.client_version = j.value("clientVersion", "");
    }

    if (j.contains("auth") && j["auth"].is_object()) {
      p.auth_token = j["auth"].value("token", "");
    } else {
      p.auth_token = j.value("authToken", "");
    }

    if (j.contains("device") && j["device"].is_object()) {
      p.device_id = j["device"].value("id", "");
    } else {
      p.device_id = j.value("deviceId", "");
    }

    return p;
  }
};

struct HelloOkPayload {
  int protocol = 3;
  std::string policy = "permissive";
  bool authenticated = true;
  int tick_interval_ms = 15000;
  bool openclaw_format = false;
  std::string server_version = "0.2.0";
  std::string conn_id;
  nlohmann::json snapshot;

  nlohmann::json ToJson() const {
    nlohmann::json server_info = {{"version", server_version}};
    if (!conn_id.empty()) {
      server_info["connId"] = conn_id;
    }

    nlohmann::json features = {
        {"methods", nlohmann::json::array({"connect.hello", "gateway.health",
                                           "gateway.status", "config.get",
                                           "config.set", "config.reload",
                                           "agent.request", "agent.stop",
                                           "sessions.list", "sessions.history",
                                           "sessions.delete", "sessions.reset",
                                           "sessions.patch", "sessions.compact",
                                           "channels.list", "channels.status",
                                           "chain.execute", "skills.status",
                                           "skills.install", "cron.list",
                                           "cron.add", "cron.remove",
                                           "cron.update", "cron.run",
                                           "cron.runs", "memory.status",
                                           "memory.search", "exec.approval.request"})}};

    return {{"protocol", protocol},
            {"policy", policy},
            {"authenticated", authenticated},
            {"tickIntervalMs", tick_interval_ms},
            {"openclawFormat", openclaw_format},
            {"serverInfo", server_info},
            {"features", features},
            {"snapshot", snapshot}};
  }
};

}  // namespace quantclaw::gateway