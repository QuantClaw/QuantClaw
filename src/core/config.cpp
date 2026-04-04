// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace quantclaw {

// Static member definition
std::string QuantClawConfig::config_path_override_;

// ---------------------------------------------------------------------------
// ${VAR} environment variable substitution
// ---------------------------------------------------------------------------

static std::string substitute_env_vars(const std::string& input) {
  static const std::regex env_re(R"(\$\{([^}]+)\})");
  std::string result;
  auto begin = std::sregex_iterator(input.begin(), input.end(), env_re);
  auto end = std::sregex_iterator();

  size_t last_pos = 0;
  for (auto it = begin; it != end; ++it) {
    auto& match = *it;
    const auto match_pos = static_cast<size_t>(match.position());
    const auto match_len = static_cast<size_t>(match.length());
    result.append(input, last_pos, match_pos - last_pos);
    std::string var_name = match[1].str();
    const char* env_val = std::getenv(var_name.c_str());
    if (env_val) {
      result.append(env_val);
    }
    // If env var not set, replace with empty string (same as OpenClaw)
    last_pos = match_pos + match_len;
  }
  result.append(input, last_pos, std::string::npos);
  return result;
}

// ---------------------------------------------------------------------------
// JSON5 preprocessing: strip comments and trailing commas
// ---------------------------------------------------------------------------

static std::string strip_json5(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  size_t i = 0;
  const size_t len = input.size();

  while (i < len) {
    // --- Inside a string literal: copy verbatim, respecting escapes ---
    if (input[i] == '"') {
      out += '"';
      ++i;
      while (i < len && input[i] != '"') {
        if (input[i] == '\\' && i + 1 < len) {
          out += input[i];
          out += input[i + 1];
          i += 2;
        } else {
          out += input[i];
          ++i;
        }
      }
      if (i < len) {
        out += '"';  // closing quote
        ++i;
      }
      continue;
    }

    // --- Line comment: // ---
    if (i + 1 < len && input[i] == '/' && input[i + 1] == '/') {
      i += 2;
      while (i < len && input[i] != '\n')
        ++i;
      continue;
    }

    // --- Block comment: /* ... */ ---
    if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/'))
        ++i;
      if (i + 1 < len)
        i += 2;  // skip */
      continue;
    }

    out += input[i];
    ++i;
  }

  // --- Strip trailing commas before } or ] ---
  std::string result;
  result.reserve(out.size());
  for (size_t j = 0; j < out.size(); ++j) {
    if (out[j] == ',') {
      // Look ahead past whitespace for } or ]
      size_t k = j + 1;
      while (k < out.size() && (out[k] == ' ' || out[k] == '\t' ||
                                out[k] == '\n' || out[k] == '\r')) {
        ++k;
      }
      if (k < out.size() && (out[k] == '}' || out[k] == ']')) {
        continue;  // skip trailing comma
      }
    }
    result += out[j];
  }

  return result;
}

static void expand_env_in_json(nlohmann::json& j) {
  if (j.is_string()) {
    auto& s = j.get_ref<std::string&>();
    if (s.find("${") != std::string::npos) {
      s = substitute_env_vars(s);
    }
  } else if (j.is_object()) {
    for (auto& [key, value] : j.items()) {
      expand_env_in_json(value);
    }
  } else if (j.is_array()) {
    for (auto& element : j) {
      expand_env_in_json(element);
    }
  }
}

AgentConfig AgentConfig::FromJson(const nlohmann::json& json) {
  AgentConfig config;
  // model can be a string or an object (primary/fallbacks) — only parse string
  // here
  if (json.contains("model") && json["model"].is_string()) {
    config.model = json["model"].get<std::string>();
  }
  config.max_iterations = json.value(
      "maxIterations", json.value("max_iterations", kDefaultMaxIterations));
  config.temperature = json.value("temperature", kDefaultTemperature);
  config.max_tokens =
      json.value("maxTokens", json.value("max_tokens", kDefaultMaxTokens));
  config.context_window = json.value(
      "contextWindow", json.value("context_window", kDefaultContextWindow));
  config.thinking = json.value("thinking", "off");
  config.fallbacks = json.value("fallbacks", std::vector<std::string>{});

  // Compaction settings
  config.auto_compact =
      json.value("autoCompact", json.value("auto_compact", true));
  config.compact_max_messages =
      json.value("compactMaxMessages", json.value("compact_max_messages",
                                                  kDefaultCompactMaxMessages));
  config.compact_keep_recent =
      json.value("compactKeepRecent",
                 json.value("compact_keep_recent", kDefaultCompactKeepRecent));
  config.compact_max_tokens =
      json.value("compactMaxTokens",
                 json.value("compact_max_tokens", kDefaultCompactMaxTokens));
  return config;
}

ModelCost ModelCost::FromJson(const nlohmann::json& json) {
  ModelCost c;
  c.input = json.value("input", 0.0);
  c.output = json.value("output", 0.0);
  c.cache_read = json.value("cacheRead", json.value("cache_read", 0.0));
  c.cache_write = json.value("cacheWrite", json.value("cache_write", 0.0));
  return c;
}

ModelDefinition ModelDefinition::FromJson(const nlohmann::json& json) {
  ModelDefinition m;
  m.id = json.value("id", "");
  m.name = json.value("name", "");
  m.reasoning = json.value("reasoning", false);
  m.input = json.value("input", std::vector<std::string>{"text"});
  if (json.contains("cost") && json["cost"].is_object()) {
    m.cost = ModelCost::FromJson(json["cost"]);
  }
  m.context_window =
      json.value("contextWindow", json.value("context_window", 0));
  m.max_tokens = json.value("maxTokens", json.value("max_tokens", 0));
  return m;
}

ModelEntryConfig ModelEntryConfig::FromJson(const nlohmann::json& json) {
  ModelEntryConfig c;
  c.alias = json.value("alias", "");
  if (json.contains("params") && json["params"].is_object()) {
    c.params = json["params"];
  }
  return c;
}

AuthProfileConfig AuthProfileConfig::FromJson(const nlohmann::json& json) {
  AuthProfileConfig p;
  p.id = json.value("id", "");
  p.api_key = json.value("apiKey", json.value("api_key", ""));
  p.api_key_env = json.value("apiKeyEnv", json.value("api_key_env", ""));
  p.priority = json.value("priority", 0);
  // Resolve env var if api_key is empty but env var name is set
  if (p.api_key.empty() && !p.api_key_env.empty()) {
    const char* env_val = std::getenv(p.api_key_env.c_str());
    if (env_val)
      p.api_key = env_val;
  }
  return p;
}

ProviderConfig ProviderConfig::FromJson(const nlohmann::json& json) {
  ProviderConfig config;
  config.api_key = json.value("apiKey", json.value("api_key", ""));
  config.base_url = json.value("baseUrl", json.value("base_url", ""));
  config.api = json.value("api", "");
  config.timeout = json.value("timeout", kDefaultProviderTimeoutSec);
  if (json.contains("models") && json["models"].is_array()) {
    for (const auto& m : json["models"]) {
      config.models.push_back(ModelDefinition::FromJson(m));
    }
  }
  // Parse profiles array for multi-key rotation
  if (json.contains("profiles") && json["profiles"].is_array()) {
    std::set<std::string> seen_ids;
    int auto_idx = 0;
    for (const auto& p : json["profiles"]) {
      auto profile = AuthProfileConfig::FromJson(p);
      // Assign auto-generated id if empty
      if (profile.id.empty()) {
        profile.id = "profile_" + std::to_string(auto_idx);
      }
      // Skip duplicate ids
      if (seen_ids.count(profile.id)) {
        continue;
      }
      seen_ids.insert(profile.id);
      config.profiles.push_back(std::move(profile));
      ++auto_idx;
    }
  }
  return config;
}

ChannelConfig ChannelConfig::FromJson(const nlohmann::json& json) {
  ChannelConfig config;
  config.enabled = json.value("enabled", false);
  config.token = json.value("token", "");
  if (json.contains("allowed_ids") && json["allowed_ids"].is_array()) {
    config.allowed_ids = json["allowed_ids"].get<std::vector<std::string>>();
  } else if (json.contains("allowedIds") && json["allowedIds"].is_array()) {
    config.allowed_ids = json["allowedIds"].get<std::vector<std::string>>();
  }
  // Store the full raw JSON so platform-specific fields are preserved
  config.raw = json;
  return config;
}

ToolConfig ToolConfig::FromJson(const nlohmann::json& json) {
  ToolConfig config;
  config.enabled = json.value("enabled", true);
  config.allowed_paths =
      json.value("allowed_paths", std::vector<std::string>{});
  config.denied_paths = json.value("denied_paths", std::vector<std::string>{});
  config.allowed_cmds = json.value("allowed_cmds", std::vector<std::string>{});
  config.denied_cmds = json.value("denied_cmds", std::vector<std::string>{});
  config.timeout = json.value("timeout", kDefaultToolTimeoutSec);
  return config;
}

ToolPermissionConfig
ToolPermissionConfig::FromJson(const nlohmann::json& json) {
  ToolPermissionConfig config;
  config.allow = json.value(
      "allow", std::vector<std::string>{"group:fs", "group:runtime"});
  config.deny = json.value("deny", std::vector<std::string>{});
  return config;
}

MCPServerConfig MCPServerConfig::FromJson(const nlohmann::json& json) {
  MCPServerConfig config;
  config.name = json.value("name", "");
  config.url = json.value("url", "");
  config.timeout = json.value("timeout", kDefaultMcpTimeoutSec);
  return config;
}

MCPConfig MCPConfig::FromJson(const nlohmann::json& json) {
  MCPConfig config;
  if (json.contains("servers") && json["servers"].is_array()) {
    for (const auto& server_json : json["servers"]) {
      config.servers.push_back(MCPServerConfig::FromJson(server_json));
    }
  }
  return config;
}

SkillEntryConfig SkillEntryConfig::FromJson(const nlohmann::json& json) {
  SkillEntryConfig config;
  config.enabled = json.value("enabled", true);
  return config;
}

SkillsLoadConfig SkillsLoadConfig::FromJson(const nlohmann::json& json) {
  SkillsLoadConfig config;
  config.extra_dirs = json.value("extraDirs", std::vector<std::string>{});
  return config;
}

SkillsConfig SkillsConfig::FromJson(const nlohmann::json& json) {
  SkillsConfig config;

  // OpenClaw simple format: skills.path, skills.autoApprove, skills.configs
  config.path = json.value("path", "");
  config.auto_approve = json.value("autoApprove", std::vector<std::string>{});
  if (json.contains("configs") && json["configs"].is_object()) {
    config.configs = json["configs"];
  }

  // QuantClaw format: skills.load, skills.entries
  if (json.contains("load") && json["load"].is_object()) {
    config.load = SkillsLoadConfig::FromJson(json["load"]);
  }
  // OpenClaw path → extraDirs compatibility
  if (!config.path.empty() && config.load.extra_dirs.empty()) {
    config.load.extra_dirs.push_back(config.path);
  }

  if (json.contains("entries") && json["entries"].is_object()) {
    for (const auto& [key, value] : json["entries"].items()) {
      config.entries[key] = SkillEntryConfig::FromJson(value);
    }
  }
  return config;
}

GatewayConfig GatewayConfig::FromJson(const nlohmann::json& json) {
  GatewayConfig config;
  config.port =
      json.value("port", kDefaultGatewayPort);  // QuantClaw WebSocket RPC port
  config.bind = json.value("bind", "loopback");
  if (json.contains("auth")) {
    config.auth = GatewayAuthConfig::FromJson(json["auth"]);
  }
  if (json.contains("controlUi")) {
    config.control_ui = GatewayControlUiConfig::FromJson(json["controlUi"]);
  }
  return config;
}

QuantClawConfig QuantClawConfig::FromJson(const nlohmann::json& json) {
  // Expand ${VAR} references in a mutable copy
  nlohmann::json expanded = json;
  expand_env_in_json(expanded);

  return FromJsonExpanded(expanded);
}

QuantClawConfig QuantClawConfig::FromJsonExpanded(const nlohmann::json& json) {
  QuantClawConfig config;

  // ================================================================
  // OpenClaw "system" section → system config + gateway port
  // ================================================================
  if (json.contains("system") && json["system"].is_object()) {
    config.system = SystemConfig::FromJson(json["system"]);
    // system.port overrides gateway.controlUi.port (HTTP port)
    if (config.system.port > 0) {
      config.gateway.control_ui.port = config.system.port;
    }
  }

  // ================================================================
  // OpenClaw "llm" section → agent + providers (flat, single provider)
  // Format: { "provider": "openai", "model": "anthropic/claude-sonnet-4-6",
  // "apiKey": "...", "baseUrl": "...", "temperature": 0.2, "maxTokens": 2048 }
  // ================================================================
  if (json.contains("llm") && json["llm"].is_object()) {
    const auto& llm = json["llm"];
    std::string provider_name = llm.value("provider", "openai");

    config.agent.model = llm.value("model", "anthropic/claude-sonnet-4-6");
    config.agent.temperature = llm.value("temperature", kDefaultTemperature);
    config.agent.max_tokens = llm.value("maxTokens", kDefaultMaxTokens);

    ProviderConfig prov;
    prov.api_key = llm.value("apiKey", "");
    prov.base_url = llm.value("baseUrl", "");
    prov.timeout = llm.value("timeout", kDefaultProviderTimeoutSec);
    config.providers[provider_name] = prov;
  }

  // ================================================================
  // QuantClaw "agent" section (takes priority over llm if both exist)
  // ================================================================
  if (json.contains("agent") && json["agent"].is_object()) {
    config.agent = AgentConfig::FromJson(json["agent"]);
  } else if (json.contains("agents") && json["agents"].contains("defaults")) {
    // Legacy format
    config.agent = AgentConfig::FromJson(json["agents"]["defaults"]);
  }

  // ================================================================
  // Gateway
  // ================================================================
  if (json.contains("gateway") && json["gateway"].is_object()) {
    config.gateway = GatewayConfig::FromJson(json["gateway"]);
  }

  // ================================================================
  // Providers (QuantClaw multi-provider format, merges with llm-derived
  // provider)
  // ================================================================
  if (json.contains("providers") && json["providers"].is_object()) {
    for (const auto& [key, value] : json["providers"].items()) {
      config.providers[key] = ProviderConfig::FromJson(value);
    }
  }

  // ================================================================
  // Models providers (OpenClaw multi-model format: models.providers)
  // ================================================================
  if (json.contains("models") && json["models"].is_object() &&
      json["models"].contains("providers") &&
      json["models"]["providers"].is_object()) {
    for (const auto& [id, val] : json["models"]["providers"].items()) {
      config.model_providers[id] = ProviderConfig::FromJson(val);
    }
  }

  // ================================================================
  // Model aliases (agents.defaults.models)
  // ================================================================
  if (json.contains("agents") && json["agents"].is_object() &&
      json["agents"].contains("defaults") &&
      json["agents"]["defaults"].is_object() &&
      json["agents"]["defaults"].contains("models") &&
      json["agents"]["defaults"]["models"].is_object()) {
    for (const auto& [key, val] :
         json["agents"]["defaults"]["models"].items()) {
      config.model_entries[key] = ModelEntryConfig::FromJson(val);
    }
  }

  // ================================================================
  // Agent model object form (agents.defaults.model as object with
  // primary/fallbacks)
  // ================================================================
  if (json.contains("agents") && json["agents"].is_object() &&
      json["agents"].contains("defaults") &&
      json["agents"]["defaults"].is_object() &&
      json["agents"]["defaults"].contains("model") &&
      json["agents"]["defaults"]["model"].is_object()) {
    const auto& model_val = json["agents"]["defaults"]["model"];
    config.agent.model = model_val.value("primary", config.agent.model);
    if (model_val.contains("fallbacks") && model_val["fallbacks"].is_array()) {
      config.agent.fallbacks =
          model_val["fallbacks"].get<std::vector<std::string>>();
    }
  }

  // ================================================================
  // Channels — store full raw JSON per channel for adapter passthrough
  // ================================================================
  if (json.contains("channels") && json["channels"].is_object()) {
    for (const auto& [key, value] : json["channels"].items()) {
      config.channels[key] = ChannelConfig::FromJson(value);
    }
  }

  // ================================================================
  // Security (OpenClaw format)
  // ================================================================
  if (json.contains("security") && json["security"].is_object()) {
    config.security = SecurityConfig::FromJson(json["security"]);
  }

  // ================================================================
  // MCP
  // ================================================================
  if (json.contains("mcp") && json["mcp"].is_object()) {
    config.mcp = MCPConfig::FromJson(json["mcp"]);
  }

  // ================================================================
  // Skills (both OpenClaw and QuantClaw format)
  // ================================================================
  if (json.contains("skills") && json["skills"].is_object()) {
    config.skills = SkillsConfig::FromJson(json["skills"]);
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
  // Queue (command queue config, consumed by CommandQueue)
  // ================================================================
  if (json.contains("queue") && json["queue"].is_object()) {
    config.queue_config = json["queue"];
  }

  // ================================================================
  // Tools (permission allow/deny or legacy named configs)
  // ================================================================
  if (json.contains("tools") && json["tools"].is_object()) {
    const auto& tools_json = json["tools"];
    if (tools_json.contains("allow") || tools_json.contains("deny")) {
      config.tools_permission = ToolPermissionConfig::FromJson(tools_json);
    } else {
      for (const auto& [key, value] : tools_json.items()) {
        config.tools[key] = ToolConfig::FromJson(value);
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
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  std::string clean = strip_json5(content);
  return nlohmann::json::parse(clean);
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

void QuantClawConfig::SetValue(const std::string& filepath,
                               const std::string& dot_path,
                               const nlohmann::json& value) {
  std::string expanded = ExpandHome(filepath);
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

void QuantClawConfig::UnsetValue(const std::string& filepath,
                                 const std::string& dot_path) {
  std::string expanded = ExpandHome(filepath);
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

std::string QuantClawConfig::ExpandHome(const std::string& path) {
  std::string expanded = path;
  if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home)
      home = std::getenv("USERPROFILE");
#endif
    if (home) {
      expanded = (std::filesystem::path(home) / expanded.substr(2)).string();
    }
  }
  return expanded;
}

std::string QuantClawConfig::DefaultConfigPath() {
  if (!config_path_override_.empty()) {
    return config_path_override_;
  }
  return ExpandHome("~/.quantclaw/quantclaw.json");
}

void QuantClawConfig::set_config_path(const std::string& path) {
  config_path_override_ = path;
}

QuantClawConfig QuantClawConfig::LoadFromFile(const std::string& filepath) {
  std::string expanded_path = ExpandHome(filepath);

  if (!std::filesystem::exists(expanded_path)) {
    throw std::runtime_error("Config file not found: " + expanded_path);
  }

  std::ifstream file(expanded_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open config file: " + expanded_path);
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();

  std::string clean = strip_json5(content);
  nlohmann::json json = nlohmann::json::parse(clean);

  return FromJson(json);
}

int AgentConfig::DynamicMaxIterations() const {
  // Scale linearly: 32K → 32 iterations, 200K → 160 iterations
  if (context_window <= kContextWindow32K)
    return kMinMaxIterations;
  if (context_window >= kContextWindow200K)
    return kMaxMaxIterations;

  double ratio = static_cast<double>(context_window - kContextWindow32K) /
                 (kContextWindow200K - kContextWindow32K);
  return kMinMaxIterations +
         static_cast<int>(ratio * (kMaxMaxIterations - kMinMaxIterations));
}

}  // namespace quantclaw
