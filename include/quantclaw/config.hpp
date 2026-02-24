#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace quantclaw {

// --- Agent / LLM ---

struct AgentConfig {
    std::string model = "qwen-max";
    int max_iterations = 15;
    double temperature = 0.7;
    int max_tokens = 4096;

    static AgentConfig from_json(const nlohmann::json& json);
};

struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    int timeout = 30;

    static ProviderConfig from_json(const nlohmann::json& json);
};

// --- Channels (OpenClaw compatible) ---
// Stores common fields + raw JSON for platform-specific settings

struct ChannelConfig {
    bool enabled = false;
    std::string token;
    std::vector<std::string> allowed_ids;

    // Full raw JSON — passed to adapter subprocess as QUANTCLAW_CHANNEL_CONFIG
    // Contains all platform-specific fields (clientId, robotCode, dmPolicy, etc.)
    nlohmann::json raw;

    static ChannelConfig from_json(const nlohmann::json& json);
};

// --- Tools ---

struct ToolConfig {
    bool enabled = true;
    std::vector<std::string> allowed_paths;
    std::vector<std::string> denied_paths;
    std::vector<std::string> allowed_cmds;
    std::vector<std::string> denied_cmds;
    int timeout = 30;

    static ToolConfig from_json(const nlohmann::json& json);
};

struct ToolPermissionConfig {
    std::vector<std::string> allow;  // e.g. ["group:fs", "group:runtime"]
    std::vector<std::string> deny;

    static ToolPermissionConfig from_json(const nlohmann::json& json);
};

// --- MCP ---

struct MCPServerConfig {
    std::string name;
    std::string url;
    int timeout = 30;

    static MCPServerConfig from_json(const nlohmann::json& json);
};

struct MCPConfig {
    std::vector<MCPServerConfig> servers;

    static MCPConfig from_json(const nlohmann::json& json);
};

// --- Gateway ---

struct GatewayAuthConfig {
    std::string mode = "token";  // "token" | "none"
    std::string token;

    static GatewayAuthConfig from_json(const nlohmann::json& json) {
        GatewayAuthConfig c;
        c.mode = json.value("mode", "token");
        c.token = json.value("token", "");
        return c;
    }
};

struct GatewayControlUiConfig {
    bool enabled = true;
    int port = 18790;

    static GatewayControlUiConfig from_json(const nlohmann::json& json) {
        GatewayControlUiConfig c;
        c.enabled = json.value("enabled", true);
        c.port = json.value("port", 18790);
        return c;
    }
};

struct GatewayConfig {
    int port = 18789;
    std::string bind = "loopback";
    GatewayAuthConfig auth;
    GatewayControlUiConfig control_ui;

    static GatewayConfig from_json(const nlohmann::json& json);
};

// --- System (OpenClaw format) ---

struct SystemConfig {
    std::string name = "QuantClaw";
    std::string version = "0.2.0";
    std::string log_level = "info";
    int port = 0;  // 0 = not set, use gateway.port

    static SystemConfig from_json(const nlohmann::json& json) {
        SystemConfig c;
        c.name = json.value("name", "QuantClaw");
        c.version = json.value("version", "0.2.0");
        c.log_level = json.value("logLevel", "info");
        c.port = json.value("port", 0);
        return c;
    }
};

// --- Security (OpenClaw format) ---

struct SecurityConfig {
    std::string permission_level = "auto";  // "auto" | "strict" | "permissive"
    bool allow_local_execute = true;

    static SecurityConfig from_json(const nlohmann::json& json) {
        SecurityConfig c;
        c.permission_level = json.value("permissionLevel", "auto");
        c.allow_local_execute = json.value("allowLocalExecute", true);
        return c;
    }
};

// --- Skills ---

struct SkillEntryConfig {
    bool enabled = true;
    static SkillEntryConfig from_json(const nlohmann::json& json);
};

struct SkillsLoadConfig {
    std::vector<std::string> extra_dirs;
    static SkillsLoadConfig from_json(const nlohmann::json& json);
};

struct SkillsConfig {
    std::string path;  // OpenClaw: skills.path
    std::vector<std::string> auto_approve;  // OpenClaw: skills.autoApprove
    SkillsLoadConfig load;
    std::unordered_map<std::string, SkillEntryConfig> entries;
    nlohmann::json configs;  // OpenClaw: skills.configs

    static SkillsConfig from_json(const nlohmann::json& json);
};

// --- Top-level config ---

struct QuantClawConfig {
    SystemConfig system;
    AgentConfig agent;
    GatewayConfig gateway;
    SecurityConfig security;
    std::unordered_map<std::string, ProviderConfig> providers;
    std::unordered_map<std::string, ChannelConfig> channels;
    ToolPermissionConfig tools_permission;
    MCPConfig mcp;
    SkillsConfig skills;

    // Legacy compatibility
    std::unordered_map<std::string, ToolConfig> tools;

    static QuantClawConfig from_json(const nlohmann::json& json);
    static QuantClawConfig load_from_file(const std::string& filepath);

    static std::string expand_home(const std::string& path);
    static std::string default_config_path();

    // Override the default config path (set via --config CLI flag)
    static void set_config_path(const std::string& path);

private:
    static std::string config_path_override_;
};

} // namespace quantclaw
