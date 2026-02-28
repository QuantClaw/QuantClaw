#include "quantclaw/config.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace quantclaw {

AgentConfig AgentConfig::from_json(const nlohmann::json& json) {
    AgentConfig config;
    config.model = json.value("model", "qwen-max");
    config.max_iterations = json.value("maxIterations", json.value("max_iterations", 15));
    config.temperature = json.value("temperature", 0.7);
    config.max_tokens = json.value("maxTokens", json.value("max_tokens", 4096));
    return config;
}

ProviderConfig ProviderConfig::from_json(const nlohmann::json& json) {
    ProviderConfig config;
    config.api_key = json.value("apiKey", json.value("api_key", ""));
    config.base_url = json.value("baseUrl", json.value("base_url", ""));
    config.timeout = json.value("timeout", 30);
    return config;
}

ChannelConfig ChannelConfig::from_json(const nlohmann::json& json) {
    ChannelConfig config;
    config.enabled = json.value("enabled", false);
    config.token = json.value("token", "");
    config.allowed_ids = json.value("allowed_ids", std::vector<std::string>{});
    // Store the full raw JSON so platform-specific fields are preserved
    config.raw = json;
    return config;
}

ToolConfig ToolConfig::from_json(const nlohmann::json& json) {
    ToolConfig config;
    config.enabled = json.value("enabled", true);
    config.allowed_paths = json.value("allowed_paths", std::vector<std::string>{});
    config.denied_paths = json.value("denied_paths", std::vector<std::string>{});
    config.allowed_cmds = json.value("allowed_cmds", std::vector<std::string>{});
    config.denied_cmds = json.value("denied_cmds", std::vector<std::string>{});
    config.timeout = json.value("timeout", 30);
    return config;
}

ToolPermissionConfig ToolPermissionConfig::from_json(const nlohmann::json& json) {
    ToolPermissionConfig config;
    config.allow = json.value("allow", std::vector<std::string>{"group:fs", "group:runtime"});
    config.deny = json.value("deny", std::vector<std::string>{});
    return config;
}

MCPServerConfig MCPServerConfig::from_json(const nlohmann::json& json) {
    MCPServerConfig config;
    config.name = json.value("name", "");
    config.url = json.value("url", "");
    config.timeout = json.value("timeout", 30);
    return config;
}

MCPConfig MCPConfig::from_json(const nlohmann::json& json) {
    MCPConfig config;
    if (json.contains("servers") && json["servers"].is_array()) {
        for (const auto& server_json : json["servers"]) {
            config.servers.push_back(MCPServerConfig::from_json(server_json));
        }
    }
    return config;
}

SkillEntryConfig SkillEntryConfig::from_json(const nlohmann::json& json) {
    SkillEntryConfig config;
    config.enabled = json.value("enabled", true);
    return config;
}

SkillsLoadConfig SkillsLoadConfig::from_json(const nlohmann::json& json) {
    SkillsLoadConfig config;
    config.extra_dirs = json.value("extraDirs", std::vector<std::string>{});
    return config;
}

SkillsConfig SkillsConfig::from_json(const nlohmann::json& json) {
    SkillsConfig config;

    // OpenClaw simple format: skills.path, skills.autoApprove, skills.configs
    config.path = json.value("path", "");
    config.auto_approve = json.value("autoApprove", std::vector<std::string>{});
    if (json.contains("configs") && json["configs"].is_object()) {
        config.configs = json["configs"];
    }

    // QuantClaw format: skills.load, skills.entries
    if (json.contains("load") && json["load"].is_object()) {
        config.load = SkillsLoadConfig::from_json(json["load"]);
    }
    // OpenClaw path → extraDirs compatibility
    if (!config.path.empty() && config.load.extra_dirs.empty()) {
        config.load.extra_dirs.push_back(config.path);
    }

    if (json.contains("entries") && json["entries"].is_object()) {
        for (const auto& [key, value] : json["entries"].items()) {
            config.entries[key] = SkillEntryConfig::from_json(value);
        }
    }
    return config;
}

GatewayConfig GatewayConfig::from_json(const nlohmann::json& json) {
    GatewayConfig config;
    config.port = json.value("port", 18789);
    config.bind = json.value("bind", "loopback");
    if (json.contains("auth")) {
        config.auth = GatewayAuthConfig::from_json(json["auth"]);
    }
    if (json.contains("controlUi")) {
        config.control_ui = GatewayControlUiConfig::from_json(json["controlUi"]);
    }
    return config;
}

QuantClawConfig QuantClawConfig::from_json(const nlohmann::json& json) {
    QuantClawConfig config;

    // ================================================================
    // OpenClaw "system" section → system config + gateway port
    // ================================================================
    if (json.contains("system") && json["system"].is_object()) {
        config.system = SystemConfig::from_json(json["system"]);
        // system.port overrides gateway.controlUi.port (HTTP port)
        if (config.system.port > 0) {
            config.gateway.control_ui.port = config.system.port;
        }
    }

    // ================================================================
    // OpenClaw "llm" section → agent + providers (flat, single provider)
    // Format: { "provider": "openai", "model": "qwen-max", "apiKey": "...", "baseUrl": "...", "temperature": 0.2, "maxTokens": 2048 }
    // ================================================================
    if (json.contains("llm") && json["llm"].is_object()) {
        const auto& llm = json["llm"];
        std::string provider_name = llm.value("provider", "openai");

        config.agent.model = llm.value("model", "qwen-max");
        config.agent.temperature = llm.value("temperature", 0.7);
        config.agent.max_tokens = llm.value("maxTokens", 4096);

        ProviderConfig prov;
        prov.api_key = llm.value("apiKey", "");
        prov.base_url = llm.value("baseUrl", "");
        prov.timeout = llm.value("timeout", 30);
        config.providers[provider_name] = prov;
    }

    // ================================================================
    // QuantClaw "agent" section (takes priority over llm if both exist)
    // ================================================================
    if (json.contains("agent") && json["agent"].is_object()) {
        config.agent = AgentConfig::from_json(json["agent"]);
    } else if (json.contains("agents") && json["agents"].contains("defaults")) {
        // Legacy format
        config.agent = AgentConfig::from_json(json["agents"]["defaults"]);
    }

    // ================================================================
    // Gateway
    // ================================================================
    if (json.contains("gateway") && json["gateway"].is_object()) {
        config.gateway = GatewayConfig::from_json(json["gateway"]);
    }

    // ================================================================
    // Providers (QuantClaw multi-provider format, merges with llm-derived provider)
    // ================================================================
    if (json.contains("providers") && json["providers"].is_object()) {
        for (const auto& [key, value] : json["providers"].items()) {
            config.providers[key] = ProviderConfig::from_json(value);
        }
    }

    // ================================================================
    // Channels — store full raw JSON per channel for adapter passthrough
    // ================================================================
    if (json.contains("channels") && json["channels"].is_object()) {
        for (const auto& [key, value] : json["channels"].items()) {
            config.channels[key] = ChannelConfig::from_json(value);
        }
    }

    // ================================================================
    // Security (OpenClaw format)
    // ================================================================
    if (json.contains("security") && json["security"].is_object()) {
        config.security = SecurityConfig::from_json(json["security"]);
    }

    // ================================================================
    // MCP
    // ================================================================
    if (json.contains("mcp") && json["mcp"].is_object()) {
        config.mcp = MCPConfig::from_json(json["mcp"]);
    }

    // ================================================================
    // Skills (both OpenClaw and QuantClaw format)
    // ================================================================
    if (json.contains("skills") && json["skills"].is_object()) {
        config.skills = SkillsConfig::from_json(json["skills"]);
    }

    // ================================================================
    // Plugins (raw JSON, consumed by PluginRegistry)
    // ================================================================
    if (json.contains("plugins") && json["plugins"].is_object()) {
        config.plugins_config = json["plugins"];
    }

    // ================================================================
    // Session maintenance
    // ================================================================
    if (json.contains("session") && json["session"].is_object()) {
        if (json["session"].contains("maintenance")) {
            config.session_maintenance_config = json["session"]["maintenance"];
        }
    }

    // ================================================================
    // Subagent config
    // ================================================================
    if (json.contains("subagents") && json["subagents"].is_object()) {
        config.subagent_config = json["subagents"];
    } else if (json.contains("agents") && json["agents"].is_object() &&
               json["agents"].contains("defaults") &&
               json["agents"]["defaults"].contains("subagents")) {
        config.subagent_config = json["agents"]["defaults"]["subagents"];
    }

    // ================================================================
    // Browser
    // ================================================================
    if (json.contains("browser") && json["browser"].is_object()) {
        config.browser_config = json["browser"];
    }

    // ================================================================
    // Exec approval (from tools.exec section, OpenClaw compatible)
    // ================================================================
    if (json.contains("tools") && json["tools"].is_object() &&
        json["tools"].contains("exec") && json["tools"]["exec"].is_object()) {
        config.exec_approval_config = json["tools"]["exec"];
    }

    // ================================================================
    // Tools (permission allow/deny or legacy named configs)
    // ================================================================
    if (json.contains("tools") && json["tools"].is_object()) {
        const auto& tools_json = json["tools"];
        if (tools_json.contains("allow") || tools_json.contains("deny")) {
            config.tools_permission = ToolPermissionConfig::from_json(tools_json);
        } else {
            for (const auto& [key, value] : tools_json.items()) {
                config.tools[key] = ToolConfig::from_json(value);
            }
        }
    }

    return config;
}

// ---------------------------------------------------------------------------
// Dot-path config set/unset
// ---------------------------------------------------------------------------

static std::vector<std::string> split_dot_path(const std::string& path) {
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    while (start < path.size()) {
        auto dot = path.find('.', start);
        if (dot == std::string::npos) {
            parts.push_back(path.substr(start));
            break;
        }
        parts.push_back(path.substr(start, dot - start));
        start = dot + 1;
    }
    return parts;
}

static nlohmann::json read_json_file(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        return nlohmann::json::object();
    }
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }
    nlohmann::json j;
    file >> j;
    return j;
}

static void write_json_file(const std::string& filepath,
                             const nlohmann::json& j) {
    auto parent = std::filesystem::path(filepath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    if (std::filesystem::exists(filepath)) {
        std::filesystem::copy_file(
            filepath, filepath + ".bak",
            std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write config file: " + filepath);
    }
    file << j.dump(2) << std::endl;
}

void QuantClawConfig::set_value(const std::string& filepath,
                                const std::string& dot_path,
                                const nlohmann::json& value) {
    std::string expanded = expand_home(filepath);
    auto root = read_json_file(expanded);
    auto parts = split_dot_path(dot_path);
    if (parts.empty()) {
        throw std::runtime_error("Empty config path");
    }

    nlohmann::json* node = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!node->contains(parts[i]) || !(*node)[parts[i]].is_object()) {
            (*node)[parts[i]] = nlohmann::json::object();
        }
        node = &(*node)[parts[i]];
    }
    (*node)[parts.back()] = value;
    write_json_file(expanded, root);
}

void QuantClawConfig::unset_value(const std::string& filepath,
                                  const std::string& dot_path) {
    std::string expanded = expand_home(filepath);
    auto root = read_json_file(expanded);
    auto parts = split_dot_path(dot_path);
    if (parts.empty()) {
        throw std::runtime_error("Empty config path");
    }

    nlohmann::json* node = &root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!node->contains(parts[i]) || !(*node)[parts[i]].is_object()) {
            return;  // Path doesn't exist
        }
        node = &(*node)[parts[i]];
    }
    node->erase(parts.back());
    write_json_file(expanded, root);
}

std::string QuantClawConfig::expand_home(const std::string& path) {
    std::string expanded = path;
    if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
        const char* home = std::getenv("HOME");
        if (home) {
            expanded = std::string(home) + expanded.substr(1);
        }
    }
    return expanded;
}

std::string QuantClawConfig::default_config_path() {
    return expand_home("~/.quantclaw/quantclaw.json");
}

QuantClawConfig QuantClawConfig::load_from_file(const std::string& filepath) {
    std::string expanded_path = expand_home(filepath);

    if (!std::filesystem::exists(expanded_path)) {
        throw std::runtime_error("Config file not found: " + expanded_path);
    }

    std::ifstream file(expanded_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + expanded_path);
    }

    nlohmann::json json;
    file >> json;
    file.close();

    return from_json(json);
}

} // namespace quantclaw
