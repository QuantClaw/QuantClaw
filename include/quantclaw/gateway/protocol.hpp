// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace quantclaw::gateway {

// --- Frame Types ---

enum class FrameType {
    kRequest,
    kResponse,
    kEvent
};

inline std::string FrameTypeToString(FrameType type) {
    switch (type) {
        case FrameType::kRequest:  return "req";
        case FrameType::kResponse: return "res";
        case FrameType::kEvent:    return "event";
    }
    return "unknown";
}

inline FrameType FrameTypeFromString(const std::string& str) {
    if (str == "req")   return FrameType::kRequest;
    if (str == "res")   return FrameType::kResponse;
    if (str == "event") return FrameType::kEvent;
    throw std::runtime_error("Unknown frame type: " + str);
}

// --- RPC Request ---

struct RpcRequest {
    std::string id;
    std::string method;
    nlohmann::json params;

    nlohmann::json ToJson() const {
        return {
            {"type", "req"},
            {"id", id},
            {"method", method},
            {"params", params}
        };
    }

    static RpcRequest FromJson(const nlohmann::json& j) {
        RpcRequest req;
        req.id = j.at("id").get<std::string>();
        req.method = j.at("method").get<std::string>();
        req.params = j.value("params", nlohmann::json::object());
        return req;
    }
};

// --- RPC Response ---

struct RpcResponse {
    std::string id;
    bool ok = true;
    nlohmann::json payload;
    std::string error;

    nlohmann::json ToJson() const {
        nlohmann::json j = {
            {"type", "res"},
            {"id", id},
            {"ok", ok}
        };
        if (ok) {
            j["payload"] = payload;
        } else {
            j["error"] = error;
        }
        return j;
    }

    static RpcResponse success(const std::string& id, const nlohmann::json& payload) {
        return {id, true, payload, ""};
    }

    static RpcResponse failure(const std::string& id, const std::string& error) {
        return {id, false, {}, error};
    }
};

// --- RPC Event ---

struct RpcEvent {
    std::string event;
    nlohmann::json payload;
    std::optional<uint64_t> seq;
    std::optional<uint64_t> state_version;

    nlohmann::json ToJson() const {
        nlohmann::json j = {
            {"type", "event"},
            {"event", event},
            {"payload", payload}
        };
        if (seq) j["seq"] = *seq;
        if (state_version) j["stateVersion"] = *state_version;
        return j;
    }
};

// --- Connect / Hello handshake ---

struct ConnectChallenge {
    std::string nonce;
    int64_t timestamp;

    nlohmann::json ToJson() const {
        return {
            {"type", "event"},
            {"event", "connect.challenge"},
            {"payload", {{"nonce", nonce}, {"ts", timestamp}}}
        };
    }
};

struct ConnectHelloParams {
    int min_protocol = 1;
    int max_protocol = 1;
    std::string client_name;
    std::string client_version;
    std::string role;                    // "operator" | "node"
    std::vector<std::string> scopes;     // e.g. ["operator.read", "operator.write"]
    std::string auth_token;
    std::string device_id;

    static ConnectHelloParams FromJson(const nlohmann::json& j) {
        ConnectHelloParams p;
        p.min_protocol = j.value("minProtocol", 1);
        p.max_protocol = j.value("maxProtocol", 1);
        p.role = j.value("role", "operator");
        p.scopes = j.value("scopes", std::vector<std::string>{"operator.read", "operator.write"});

        // Accept both flat (QuantClaw) and nested (OpenClaw) param formats
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
    int protocol = 1;
    std::string policy = "permissive";
    bool authenticated = true;
    int tick_interval_ms = 15000;
    bool openclaw_format = false;

    nlohmann::json ToJson() const {
        if (openclaw_format) {
            return {
                {"protocol", protocol},
                {"authenticated", authenticated},
                {"tickIntervalMs", tick_interval_ms},
                {"capabilities", nlohmann::json::array({"chat", "sessions", "tools"})}
            };
        }
        return {
            {"protocol", protocol},
            {"policy", policy},
            {"authenticated", authenticated},
            {"tickIntervalMs", tick_interval_ms}
        };
    }
};

// --- Client Connection Info ---

struct ClientConnection {
    std::string connection_id;
    std::string role;
    std::vector<std::string> scopes;
    std::string device_id;
    std::string client_name;
    std::string client_version;
    int64_t connected_at = 0;
    bool authenticated = false;
    std::string client_type = "quantclaw";  // "quantclaw" | "openclaw"
};

// --- RPC Method Names ---

namespace methods {
    constexpr const char* kConnectHello     = "connect.hello";
    constexpr const char* kGatewayHealth    = "gateway.health";
    constexpr const char* kGatewayStatus    = "gateway.status";
    constexpr const char* kConfigGet        = "config.get";
    constexpr const char* kConfigSet        = "config.set";
    constexpr const char* kConfigReload     = "config.reload";
    constexpr const char* kAgentRequest     = "agent.request";
    constexpr const char* kAgentStop        = "agent.stop";
    constexpr const char* kSessionsList     = "sessions.list";
    constexpr const char* kSessionsHistory  = "sessions.history";
    constexpr const char* kSessionsDelete   = "sessions.delete";
    constexpr const char* kSessionsReset    = "sessions.reset";
    constexpr const char* kChannelsList     = "channels.list";
    constexpr const char* kChannelsStatus   = "channels.status";
    constexpr const char* kChainExecute     = "chain.execute";

    // Session management (extended)
    constexpr const char* kSessionsPatch    = "sessions.patch";
    constexpr const char* kSessionsCompact  = "sessions.compact";

    // Skills
    constexpr const char* kSkillsStatus     = "skills.status";
    constexpr const char* kSkillsInstall    = "skills.install";

    // Cron
    constexpr const char* kCronUpdate       = "cron.update";
    constexpr const char* kCronRun          = "cron.run";
    constexpr const char* kCronRuns         = "cron.runs";

    // Exec approval
    constexpr const char* kExecApprovalReq  = "exec.approval.request";
    constexpr const char* kExecApprovals    = "exec.approvals.get";

    // Models
    constexpr const char* kModelsSet        = "models.set";

    // OpenClaw-compatible method names
    constexpr const char* kOcConnect          = "connect";
    constexpr const char* kOcChatSend         = "chat.send";
    constexpr const char* kOcChatHistory      = "chat.history";
    constexpr const char* kOcChatAbort        = "chat.abort";
    constexpr const char* kOcHealth           = "health";
    constexpr const char* kOcStatus           = "status";
    constexpr const char* kOcModelsList       = "models.list";
    constexpr const char* kOcToolsCatalog     = "tools.catalog";
    constexpr const char* kOcSessionsPreview  = "sessions.preview";
} // namespace methods

// --- Event Names ---

namespace events {
    constexpr const char* kConnectChallenge = "connect.challenge";
    constexpr const char* kTextDelta        = "agent.text_delta";
    constexpr const char* kToolUse          = "agent.tool_use";
    constexpr const char* kToolResult       = "agent.tool_result";
    constexpr const char* kMessageEnd       = "agent.message_end";
    constexpr const char* kTick             = "gateway.tick";

    // OpenClaw-compatible event names
    constexpr const char* kOcAgent = "agent";
    constexpr const char* kOcChat  = "chat";
} // namespace events

// --- Helper: Parse any frame ---

inline FrameType ParseFrameType(const nlohmann::json& j) {
    return FrameTypeFromString(j.at("type").get<std::string>());
}

} // namespace quantclaw::gateway
