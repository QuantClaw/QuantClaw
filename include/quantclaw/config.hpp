// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "quantclaw/constants.hpp"

namespace quantclaw {

// --- Agent / LLM ---

struct AgentConfig {
  std::string model = "anthropic/claude-sonnet-4-6";
  int max_iterations = kDefaultMaxIterations;
  double temperature = kDefaultTemperature;
  int max_tokens = kDefaultMaxTokens;
  int context_window = kDefaultContextWindow;  // Model context window (tokens)
  std::string thinking = "off";        // "off" | "low" | "medium" | "high"
  std::vector<std::string> fallbacks;  // Model fallback chain

  // Auto-compaction settings
  bool auto_compact = true;  // Enable automatic compaction
  int compact_max_messages =
      kDefaultCompactMaxMessages;  // Compact when history exceeds this
  int compact_keep_recent =
      kDefaultCompactKeepRecent;  // Keep this many recent messages
  int compact_max_tokens =
      kDefaultCompactMaxTokens;  // Compact when tokens exceed this

  static AgentConfig FromJson(const nlohmann::json& json);

  // Compute dynamic max iterations based on context window.
  // OpenClaw: scales linearly from kMinMaxIterations (32) at 32K
  // to kMaxMaxIterations (160) at 200K.
  int DynamicMaxIterations() const;
};

// --- Model definitions (OpenClaw multi-model format) ---

struct ModelCost {
  double input = 0;
  double output = 0;
  double cache_read = 0;
  double cache_write = 0;
  static ModelCost FromJson(const nlohmann::json& json);
};

struct ModelDefinition {
  std::string id;    // "qwen3-max"
  std::string name;  // "Qwen3 Max"
  bool reasoning = false;
  std::vector<std::string> input = {"text"};  // "text", "image"
  ModelCost cost;
  int context_window = 0;  // 128000
  int max_tokens = 0;      // 8192
  static ModelDefinition FromJson(const nlohmann::json& json);
};

struct ModelEntryConfig {
  std::string alias;      // "max", "plus", "vision"
  nlohmann::json params;  // Provider-specific API params
  static ModelEntryConfig FromJson(const nlohmann::json& json);
};

struct ProviderConfig {
  std::string api_key;
  std::string base_url;
  std::string api;  // "openai-completions", "anthropic-messages"
  int timeout = kDefaultProviderTimeoutSec;
  std::vector<ModelDefinition> models;  // Per-provider model definitions

  static ProviderConfig FromJson(const nlohmann::json& json);
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

  static ChannelConfig FromJson(const nlohmann::json& json);
};

// --- Tools ---

struct ToolConfig {
  bool enabled = true;
  std::vector<std::string> allowed_paths;
  std::vector<std::string> denied_paths;
  std::vector<std::string> allowed_cmds;
  std::vector<std::string> denied_cmds;
  int timeout = kDefaultToolTimeoutSec;

  static ToolConfig FromJson(const nlohmann::json& json);
};

struct ToolPermissionConfig {
  std::vector<std::string> allow;  // e.g. ["group:fs", "group:runtime"]
  std::vector<std::string> deny;

  static ToolPermissionConfig FromJson(const nlohmann::json& json);
};

// --- MCP ---

struct MCPServerConfig {
  std::string name;
  std::string url;
  int timeout = kDefaultMcpTimeoutSec;

  static MCPServerConfig FromJson(const nlohmann::json& json);
};

struct MCPConfig {
  std::vector<MCPServerConfig> servers;

  static MCPConfig FromJson(const nlohmann::json& json);
};

// --- Gateway ---

struct GatewayAuthConfig {
  std::string mode = "token";  // "token" | "none"
  std::string token;

  static GatewayAuthConfig FromJson(const nlohmann::json& json) {
    GatewayAuthConfig c;
    c.mode = json.value("mode", "token");
    c.token = json.value("token", "");
    return c;
  }
};

struct GatewayControlUiConfig {
  bool enabled = true;
  int port = kDefaultHttpPort;  // QuantClaw HTTP/Dashboard port

  static GatewayControlUiConfig FromJson(const nlohmann::json& json) {
    GatewayControlUiConfig c;
    c.enabled = json.value("enabled", true);
    c.port = json.value("port", kDefaultHttpPort);
    return c;
  }
};

struct GatewayConfig {
  int port = kDefaultGatewayPort;  // QuantClaw WebSocket RPC port
  std::string bind = "loopback";
  GatewayAuthConfig auth;
  GatewayControlUiConfig control_ui;

  static GatewayConfig FromJson(const nlohmann::json& json);
};

// --- System (OpenClaw format) ---

struct SystemConfig {
  std::string name = "QuantClaw";
  std::string version = "0.2.0";
  std::string log_level = "info";
  int port = 0;  // 0 = not set, use gateway.port
  int log_retention_days =
      7;  // Delete log files older than N days (0 = keep forever)
  int log_max_size_mb =
      50;  // Total log storage cap in MiB across all rotated files

  static SystemConfig FromJson(const nlohmann::json& json) {
    SystemConfig c;
    c.name = json.value("name", "QuantClaw");
    c.version = json.value("version", "0.2.0");
    c.log_level = json.value("logLevel", "info");
    c.port = json.value("port", 0);
    c.log_retention_days = json.value("logRetentionDays", 7);
    c.log_max_size_mb = json.value("logMaxSizeMb", 50);
    return c;
  }
};

// --- Security (OpenClaw format) ---

struct SecurityConfig {
  std::string permission_level = "auto";  // "auto" | "strict" | "permissive"
  bool allow_local_execute = true;

  static SecurityConfig FromJson(const nlohmann::json& json) {
    SecurityConfig c;
    c.permission_level = json.value("permissionLevel", "auto");
    c.allow_local_execute = json.value("allowLocalExecute", true);
    return c;
  }
};

// --- Skills ---

struct SkillEntryConfig {
  bool enabled = true;
  static SkillEntryConfig FromJson(const nlohmann::json& json);
};

struct SkillsLoadConfig {
  std::vector<std::string> extra_dirs;
  static SkillsLoadConfig FromJson(const nlohmann::json& json);
};

struct SkillsConfig {
  std::string path;                       // OpenClaw: skills.path
  std::vector<std::string> auto_approve;  // OpenClaw: skills.autoApprove
  SkillsLoadConfig load;
  std::unordered_map<std::string, SkillEntryConfig> entries;
  nlohmann::json configs;  // OpenClaw: skills.configs

  static SkillsConfig FromJson(const nlohmann::json& json);
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

  // Plugins raw config (plugins section from JSON)
  nlohmann::json plugins_config;

  // Session maintenance config (raw JSON, consumed by SessionMaintenance)
  nlohmann::json session_maintenance_config;

  // Subagent config (raw JSON, consumed by SubagentManager)
  nlohmann::json subagent_config;

  // Browser config (raw JSON, consumed by BrowserSession)
  nlohmann::json browser_config;

  // Exec approval config (raw JSON, consumed by ExecApprovalManager)
  nlohmann::json exec_approval_config;

  // Queue config (raw JSON, consumed by CommandQueue)
  nlohmann::json queue_config;

  // Models section (OpenClaw format: models.providers)
  std::unordered_map<std::string, ProviderConfig> model_providers;

  // Per-model aliases and params (agents.defaults.models)
  std::unordered_map<std::string, ModelEntryConfig> model_entries;

  // Legacy compatibility
  std::unordered_map<std::string, ToolConfig> tools;

  static QuantClawConfig FromJson(const nlohmann::json& json);
  static QuantClawConfig LoadFromFile(const std::string& filepath);

 private:
  // Internal: parse after ${VAR} expansion has already been applied
  static QuantClawConfig FromJsonExpanded(const nlohmann::json& json);

 public:
  // Write a dot-path value (e.g. "agent.model") into the config file.
  // Creates a backup (.bak) before writing.
  static void SetValue(const std::string& filepath, const std::string& dot_path,
                       const nlohmann::json& value);

  // Remove a dot-path key from the config file.
  static void UnsetValue(const std::string& filepath,
                         const std::string& dot_path);

  static std::string ExpandHome(const std::string& path);
  static std::string DefaultConfigPath();
};

}  // namespace quantclaw
