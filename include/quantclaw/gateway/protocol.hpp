#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace quantclaw::gateway {

// --- Frame Types ---

enum class FrameType {
    Request,
    Response,
    Event
};

inline std::string frame_type_to_string(FrameType type) {
    switch (type) {
        case FrameType::Request:  return "req";
        case FrameType::Response: return "res";
        case FrameType::Event:   return "event";
    }
    return "unknown";
}

inline FrameType frame_type_from_string(const std::string& str) {
    if (str == "req")   return FrameType::Request;
    if (str == "res")   return FrameType::Response;
    if (str == "event") return FrameType::Event;
    throw std::runtime_error("Unknown frame type: " + str);
}

// --- RPC Request ---

struct RpcRequest {
    std::string id;
    std::string method;
    nlohmann::json params;

    nlohmann::json to_json() const {
        return {
            {"type", "req"},
            {"id", id},
            {"method", method},
            {"params", params}
        };
    }

    static RpcRequest from_json(const nlohmann::json& j) {
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

    nlohmann::json to_json() const {
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

    nlohmann::json to_json() const {
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

    nlohmann::json to_json() const {
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

    static ConnectHelloParams from_json(const nlohmann::json& j) {
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

    nlohmann::json to_json() const {
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
    constexpr const char* CONNECT_HELLO     = "connect.hello";
    constexpr const char* GATEWAY_HEALTH    = "gateway.health";
    constexpr const char* GATEWAY_STATUS    = "gateway.status";
    constexpr const char* CONFIG_GET        = "config.get";
    constexpr const char* CONFIG_SET        = "config.set";
    constexpr const char* CONFIG_RELOAD     = "config.reload";
    constexpr const char* AGENT_REQUEST     = "agent.request";
    constexpr const char* AGENT_STOP        = "agent.stop";
    constexpr const char* SESSIONS_LIST     = "sessions.list";
    constexpr const char* SESSIONS_HISTORY  = "sessions.history";
    constexpr const char* SESSIONS_DELETE   = "sessions.delete";
    constexpr const char* SESSIONS_RESET    = "sessions.reset";
    constexpr const char* CHANNELS_LIST     = "channels.list";
    constexpr const char* CHAIN_EXECUTE     = "chain.execute";

    // OpenClaw-compatible method names
    constexpr const char* OC_CONNECT          = "connect";
    constexpr const char* OC_CHAT_SEND        = "chat.send";
    constexpr const char* OC_CHAT_HISTORY     = "chat.history";
    constexpr const char* OC_CHAT_ABORT       = "chat.abort";
    constexpr const char* OC_HEALTH           = "health";
    constexpr const char* OC_STATUS           = "status";
    constexpr const char* OC_MODELS_LIST      = "models.list";
    constexpr const char* OC_TOOLS_CATALOG    = "tools.catalog";
    constexpr const char* OC_SESSIONS_PREVIEW = "sessions.preview";
} // namespace methods

// --- Event Names ---

namespace events {
    constexpr const char* CONNECT_CHALLENGE = "connect.challenge";
    constexpr const char* TEXT_DELTA        = "agent.text_delta";
    constexpr const char* TOOL_USE          = "agent.tool_use";
    constexpr const char* TOOL_RESULT       = "agent.tool_result";
    constexpr const char* MESSAGE_END       = "agent.message_end";
    constexpr const char* TICK              = "gateway.tick";

    // OpenClaw-compatible event names
    constexpr const char* OC_AGENT = "agent";
    constexpr const char* OC_CHAT  = "chat";
} // namespace events

// --- Helper: Parse any frame ---

inline FrameType parse_frame_type(const nlohmann::json& j) {
    return frame_type_from_string(j.at("type").get<std::string>());
}

} // namespace quantclaw::gateway
