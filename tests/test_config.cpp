// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class ConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_config_test");
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
};

// --- Legacy format compatibility ---

TEST_F(ConfigTest, ParseLegacyFormat) {
  nlohmann::json json_config = {{"agents",
                                 {{"defaults",
                                   {{"model", "gpt-4-turbo"},
                                    {"max_iterations", 15},
                                    {"temperature", 0.7},
                                    {"max_tokens", 4096}}}}},
                                {"providers",
                                 {{"openai",
                                   {{"api_key", "test-key"},
                                    {"base_url", "https://api.openai.com/v1"},
                                    {"timeout", 30}}}}},
                                {"channels",
                                 {{"discord",
                                   {{"enabled", true},
                                    {"token", "test-token"},
                                    {"allowed_ids", {"123", "456"}}}}}},
                                {"tools",
                                 {{"filesystem",
                                   {{"enabled", true},
                                    {"allowed_paths", {"./workspace"}},
                                    {"denied_paths", {"/etc", "/sys"}}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  EXPECT_EQ(config.agent.model, "gpt-4-turbo");
  EXPECT_EQ(config.agent.max_iterations, 15);
  EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
  EXPECT_EQ(config.agent.max_tokens, 4096);

  ASSERT_TRUE(config.providers.count("openai"));
  EXPECT_EQ(config.providers.at("openai").api_key, "test-key");
  EXPECT_EQ(config.providers.at("openai").base_url,
            "https://api.openai.com/v1");
  EXPECT_EQ(config.providers.at("openai").timeout, 30);

  ASSERT_TRUE(config.channels.count("discord"));
  EXPECT_TRUE(config.channels.at("discord").enabled);
  EXPECT_EQ(config.channels.at("discord").token, "test-token");

  ASSERT_TRUE(config.tools.count("filesystem"));
  EXPECT_TRUE(config.tools.at("filesystem").enabled);
}

// --- OpenClaw format ---

TEST_F(ConfigTest, ParseOpenClawFormat) {
  nlohmann::json json_config = {
      {"agent",
       {{"model", "anthropic/claude-sonnet-4-6"},
        {"maxIterations", 15},
        {"temperature", 0.7}}},
      {"gateway",
       {{"port", 18800},
        {"bind", "loopback"},
        {"auth", {{"mode", "token"}}},
        {"controlUi", {{"enabled", true}}}}},
      {"providers",
       {{"openai",
         {{"apiKey", "sk-test"}, {"baseUrl", "https://api.openai.com/v1"}}}}},
      {"tools",
       {{"allow", {"group:fs", "group:runtime"}},
        {"deny", nlohmann::json::array()}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  EXPECT_EQ(config.agent.model, "anthropic/claude-sonnet-4-6");
  EXPECT_EQ(config.agent.max_iterations, 15);

  EXPECT_EQ(config.gateway.port, 18800);
  EXPECT_EQ(config.gateway.bind, "loopback");
  EXPECT_EQ(config.gateway.auth.mode, "token");
  EXPECT_TRUE(config.gateway.control_ui.enabled);

  ASSERT_TRUE(config.providers.count("openai"));
  EXPECT_EQ(config.providers.at("openai").api_key, "sk-test");

  ASSERT_EQ(config.tools_permission.allow.size(), 2u);
  EXPECT_EQ(config.tools_permission.allow[0], "group:fs");
}

// --- Defaults ---

TEST_F(ConfigTest, EmptyConfigUsesDefaults) {
  auto config = quantclaw::QuantClawConfig::FromJson({});

  EXPECT_EQ(config.agent.model, "anthropic/claude-sonnet-4-6");
  EXPECT_EQ(config.agent.max_iterations, 32);
  EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
  EXPECT_EQ(config.agent.max_tokens, 8192);

  EXPECT_EQ(config.gateway.port, 18800);
  EXPECT_EQ(config.gateway.bind, "loopback");
}

TEST_F(ConfigTest, PartialAgentConfig) {
  nlohmann::json json_config = {{"agent", {{"model", "gpt-3.5-turbo"}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  EXPECT_EQ(config.agent.model, "gpt-3.5-turbo");
  EXPECT_EQ(config.agent.max_iterations, 32);
  EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
}

// --- Gateway config ---

TEST_F(ConfigTest, GatewayConfigDefaults) {
  quantclaw::GatewayConfig gw;
  EXPECT_EQ(gw.port, 18800);
  EXPECT_EQ(gw.bind, "loopback");
  EXPECT_EQ(gw.auth.mode, "token");
  EXPECT_TRUE(gw.control_ui.enabled);
}

TEST_F(ConfigTest, GatewayConfigFromJson) {
  nlohmann::json j = {{"port", 9999},
                      {"bind", "0.0.0.0"},
                      {"auth", {{"mode", "none"}}},
                      {"controlUi", {{"enabled", false}}}};

  auto gw = quantclaw::GatewayConfig::FromJson(j);
  EXPECT_EQ(gw.port, 9999);
  EXPECT_EQ(gw.bind, "0.0.0.0");
  EXPECT_EQ(gw.auth.mode, "none");
  EXPECT_FALSE(gw.control_ui.enabled);
}

// --- File loading ---

TEST_F(ConfigTest, LoadFromFile) {
  auto config_path = test_dir_ / "quantclaw.json";
  std::ofstream f(config_path);
  f << R"({
        "agent": {"model": "test-model"},
        "gateway": {"port": 12345}
    })";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path.string());
  EXPECT_EQ(config.agent.model, "test-model");
  EXPECT_EQ(config.gateway.port, 12345);
}

TEST_F(ConfigTest, LoadFromMissingFile) {
  EXPECT_THROW(
      quantclaw::QuantClawConfig::LoadFromFile("/nonexistent/config.json"),
      std::runtime_error);
}

// --- Helper methods ---

TEST_F(ConfigTest, ExpandHome) {
  std::string path = "~/test/path";
  std::string expanded = quantclaw::QuantClawConfig::ExpandHome(path);

  EXPECT_NE(expanded.substr(0, 2), "~/");
  EXPECT_TRUE(expanded.find("/test/path") != std::string::npos);
}

TEST_F(ConfigTest, DefaultConfigPath) {
  std::string path = quantclaw::QuantClawConfig::DefaultConfigPath();
  EXPECT_TRUE(path.find(".quantclaw/quantclaw.json") != std::string::npos);
}

// --- Auth token parsing ---

TEST_F(ConfigTest, GatewayAuthTokenFromJson) {
  nlohmann::json j = {{"mode", "token"}, {"token", "secret-auth-token-123"}};

  auto auth = quantclaw::GatewayAuthConfig::FromJson(j);
  EXPECT_EQ(auth.mode, "token");
  EXPECT_EQ(auth.token, "secret-auth-token-123");
}

TEST_F(ConfigTest, GatewayAuthTokenDefaultsEmpty) {
  nlohmann::json j = {{"mode", "token"}};

  auto auth = quantclaw::GatewayAuthConfig::FromJson(j);
  EXPECT_EQ(auth.mode, "token");
  EXPECT_TRUE(auth.token.empty());
}

TEST_F(ConfigTest, GatewayAuthNoneMode) {
  nlohmann::json j = {{"mode", "none"}};

  auto auth = quantclaw::GatewayAuthConfig::FromJson(j);
  EXPECT_EQ(auth.mode, "none");
}

TEST_F(ConfigTest, FullConfigWithAuthToken) {
  nlohmann::json json_config = {
      {"gateway",
       {{"port", 18800},
        {"auth", {{"mode", "token"}, {"token", "my-secret"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  EXPECT_EQ(config.gateway.auth.mode, "token");
  EXPECT_EQ(config.gateway.auth.token, "my-secret");
}

TEST_F(ConfigTest, MCPConfigParsing) {
  nlohmann::json json_config = {{"mcp",
                                 {{"servers",
                                   {{{"name", "test-server"},
                                     {"url", "http://localhost:3000"},
                                     {"timeout", 60}}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  ASSERT_EQ(config.mcp.servers.size(), 1u);
  EXPECT_EQ(config.mcp.servers[0].name, "test-server");
  EXPECT_EQ(config.mcp.servers[0].url, "http://localhost:3000");
  EXPECT_EQ(config.mcp.servers[0].timeout, 60);
}

// --- Skills config ---

TEST_F(ConfigTest, SkillsConfigParsing) {
  nlohmann::json json_config = {
      {"skills",
       {{"load", {{"extraDirs", {"/path/to/skills", "/another/path"}}}},
        {"entries",
         {{"discord", {{"enabled", false}}},
          {"weather", {{"enabled", true}}}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  ASSERT_EQ(config.skills.load.extra_dirs.size(), 2u);
  EXPECT_EQ(config.skills.load.extra_dirs[0], "/path/to/skills");
  EXPECT_EQ(config.skills.load.extra_dirs[1], "/another/path");

  ASSERT_TRUE(config.skills.entries.count("discord"));
  EXPECT_FALSE(config.skills.entries.at("discord").enabled);

  ASSERT_TRUE(config.skills.entries.count("weather"));
  EXPECT_TRUE(config.skills.entries.at("weather").enabled);
}

TEST_F(ConfigTest, SkillsConfigDefaults) {
  auto config = quantclaw::QuantClawConfig::FromJson({});

  EXPECT_TRUE(config.skills.load.extra_dirs.empty());
  EXPECT_TRUE(config.skills.entries.empty());
}

// --- Config file watcher detection ---

TEST_F(ConfigTest, ConfigFileWatcher_DetectsChange) {
  auto config_path = test_dir_ / "watcher_test.json";

  // Write initial config
  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "initial-model"}})";
  }

  auto mtime1 = std::filesystem::last_write_time(config_path);

  // Wait >1s so filesystem mtime (which may have 1s resolution) changes
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "updated-model"}})";
  }

  auto mtime2 = std::filesystem::last_write_time(config_path);

  EXPECT_NE(mtime1, mtime2);

  // Verify the updated content loads correctly
  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path.string());
  EXPECT_EQ(config.agent.model, "updated-model");
}

// --- Config reload propagates to AgentLoop ---

// Minimal mock for this test
class ConfigReloadMockLLM : public quantclaw::LLMProvider {
 public:
  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest&) override {
    quantclaw::ChatCompletionResponse resp;
    resp.content = "mock";
    resp.finish_reason = "stop";
    return resp;
  }
  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest&,
      std::function<void(const quantclaw::ChatCompletionResponse&)> cb)
      override {
    quantclaw::ChatCompletionResponse end;
    end.content = "mock";
    end.is_stream_end = true;
    cb(end);
  }
  std::string GetProviderName() const override {
    return "config-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock"};
  }
};

TEST_F(ConfigTest, ConfigReload_PropagatesChanges) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  auto logger =
      std::make_shared<spdlog::logger>("config_reload_test", null_sink);

  auto workspace_dir = test_dir_ / "workspace";
  std::filesystem::create_directories(workspace_dir);

  auto memory_manager =
      std::make_shared<quantclaw::MemoryManager>(workspace_dir, logger);
  auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger);
  auto tool_registry = std::make_shared<quantclaw::ToolRegistry>(logger);
  auto mock_llm = std::make_shared<ConfigReloadMockLLM>();

  quantclaw::AgentConfig initial_config;
  initial_config.model = "initial-model";
  initial_config.max_iterations = 5;
  initial_config.temperature = 0.5;

  auto agent_loop = std::make_shared<quantclaw::AgentLoop>(
      memory_manager, skill_loader, tool_registry, mock_llm, initial_config,
      logger);

  EXPECT_EQ(agent_loop->GetConfig().model, "initial-model");
  EXPECT_EQ(agent_loop->GetConfig().max_iterations, 5);

  // Simulate reload: set new config
  quantclaw::AgentConfig new_config;
  new_config.model = "reloaded-model";
  new_config.max_iterations = 20;
  new_config.temperature = 0.9;

  agent_loop->SetConfig(new_config);

  EXPECT_EQ(agent_loop->GetConfig().model, "reloaded-model");
  EXPECT_EQ(agent_loop->GetConfig().max_iterations, 20);
  EXPECT_DOUBLE_EQ(agent_loop->GetConfig().temperature, 0.9);
}

// --- Config SetValue / UnsetValue ---

TEST_F(ConfigTest, SetValue_CreatesNewKey) {
  auto config_path = (test_dir_ / "set_test.json").string();

  // Start with empty config
  {
    std::ofstream f(config_path);
    f << "{}";
  }

  quantclaw::QuantClawConfig::SetValue(config_path, "agent.model", "gpt-4o");

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4o");
}

TEST_F(ConfigTest, SetValue_OverwritesExisting) {
  auto config_path = (test_dir_ / "set_overwrite.json").string();

  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "old-model", "temperature": 0.5}})";
  }

  quantclaw::QuantClawConfig::SetValue(config_path, "agent.model", "new-model");

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "new-model");
  EXPECT_DOUBLE_EQ(config.agent.temperature, 0.5);  // Other fields preserved
}

TEST_F(ConfigTest, SetValue_CreatesIntermediateObjects) {
  auto config_path = (test_dir_ / "set_nested.json").string();

  {
    std::ofstream f(config_path);
    f << "{}";
  }

  quantclaw::QuantClawConfig::SetValue(config_path, "gateway.auth.token",
                                       "my-secret");

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.gateway.auth.token, "my-secret");
}

TEST_F(ConfigTest, SetValue_CreatesBackup) {
  auto config_path = (test_dir_ / "set_backup.json").string();
  auto backup_path = config_path + ".bak";

  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "original"}})";
  }

  quantclaw::QuantClawConfig::SetValue(config_path, "agent.model", "changed");

  EXPECT_TRUE(std::filesystem::exists(backup_path));
  auto backup = quantclaw::QuantClawConfig::LoadFromFile(backup_path);
  EXPECT_EQ(backup.agent.model, "original");
}

TEST_F(ConfigTest, SetValue_NumericValue) {
  auto config_path = (test_dir_ / "set_numeric.json").string();

  {
    std::ofstream f(config_path);
    f << "{}";
  }

  quantclaw::QuantClawConfig::SetValue(config_path, "gateway.port", 9999);

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.gateway.port, 9999);
}

TEST_F(ConfigTest, UnsetValue_RemovesKey) {
  auto config_path = (test_dir_ / "unset_test.json").string();

  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "gpt-4", "temperature": 0.5}})";
  }

  quantclaw::QuantClawConfig::UnsetValue(config_path, "agent.temperature");

  // Re-read raw JSON to verify the key is gone
  std::ifstream file(config_path);
  nlohmann::json j;
  file >> j;
  EXPECT_FALSE(j["agent"].contains("temperature"));
  EXPECT_EQ(j["agent"]["model"], "gpt-4");
}

TEST_F(ConfigTest, UnsetValue_NonexistentPathIsNoop) {
  auto config_path = (test_dir_ / "unset_noop.json").string();

  {
    std::ofstream f(config_path);
    f << R"({"agent": {"model": "gpt-4"}})";
  }

  // Should not throw
  EXPECT_NO_THROW(quantclaw::QuantClawConfig::UnsetValue(
      config_path, "nonexistent.deep.path"));

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4");
}

TEST_F(ConfigTest, SetValue_OnNonexistentFile) {
  auto config_path = (test_dir_ / "new_config.json").string();

  // File doesn't exist yet
  EXPECT_FALSE(std::filesystem::exists(config_path));

  quantclaw::QuantClawConfig::SetValue(config_path, "agent.model", "gpt-4o");

  EXPECT_TRUE(std::filesystem::exists(config_path));
  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4o");
}

// --- Environment variable substitution ---

TEST_F(ConfigTest, EnvVarSubstitutionInApiKey) {
  setenv("QC_TEST_API_KEY", "sk-from-env-42", 1);

  nlohmann::json json_config = {
      {"providers",
       {{"openai",
         {{"apiKey", "${QC_TEST_API_KEY}"},
          {"baseUrl", "https://api.openai.com/v1"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  EXPECT_EQ(config.providers.at("openai").api_key, "sk-from-env-42");

  unsetenv("QC_TEST_API_KEY");
}

TEST_F(ConfigTest, EnvVarSubstitutionMissing) {
  unsetenv("QC_TEST_NONEXISTENT_VAR");

  nlohmann::json json_config = {
      {"providers", {{"openai", {{"apiKey", "${QC_TEST_NONEXISTENT_VAR}"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  // Missing env var → empty string
  EXPECT_EQ(config.providers.at("openai").api_key, "");
}

TEST_F(ConfigTest, EnvVarSubstitutionMultiple) {
  setenv("QC_TEST_HOST", "api.example.com", 1);
  setenv("QC_TEST_VERSION", "v2", 1);

  nlohmann::json json_config = {
      {"providers",
       {{"openai",
         {{"baseUrl", "https://${QC_TEST_HOST}/${QC_TEST_VERSION}"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  EXPECT_EQ(config.providers.at("openai").base_url,
            "https://api.example.com/v2");

  unsetenv("QC_TEST_HOST");
  unsetenv("QC_TEST_VERSION");
}

TEST_F(ConfigTest, EnvVarSubstitutionInNestedArrays) {
  setenv("QC_TEST_SCOPE", "admin.read", 1);

  nlohmann::json json_config = {
      {"tools", {{"allow", {"group:fs", "${QC_TEST_SCOPE}"}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  ASSERT_EQ(config.tools_permission.allow.size(), 2u);
  EXPECT_EQ(config.tools_permission.allow[1], "admin.read");

  unsetenv("QC_TEST_SCOPE");
}

// --- JSON5 support (comments + trailing commas) ---

TEST_F(ConfigTest, Json5LineComments) {
  auto config_path = (test_dir_ / "json5_comments.json").string();
  std::ofstream f(config_path);
  f << R"({
  // This is a line comment
  "agent": {
    "model": "gpt-4o", // inline comment
    "maxIterations": 10
  }
})";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4o");
  EXPECT_EQ(config.agent.max_iterations, 10);
}

TEST_F(ConfigTest, Json5BlockComments) {
  auto config_path = (test_dir_ / "json5_block.json").string();
  std::ofstream f(config_path);
  f << R"({
  /* block comment */
  "agent": {
    "model": "claude-3", /* another block */
    "maxIterations": 5
  }
})";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "claude-3");
}

TEST_F(ConfigTest, Json5TrailingCommas) {
  auto config_path = (test_dir_ / "json5_trailing.json").string();
  std::ofstream f(config_path);
  f << R"({
  "agent": {
    "model": "gpt-4o",
    "maxIterations": 10,
  },
  "providers": {
    "openai": {
      "apiKey": "sk-test",
    },
  },
})";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4o");
  EXPECT_EQ(config.providers.at("openai").api_key, "sk-test");
}

TEST_F(ConfigTest, Json5CommentsAndTrailingCommasCombined) {
  auto config_path = (test_dir_ / "json5_combined.json").string();
  std::ofstream f(config_path);
  f << R"({
  // Main config
  "agent": {
    "model": "gpt-4o", // model selection
    "maxIterations": 10,
    /* "temperature": 0.5, -- disabled for now */
  },
  "providers": {
    "openai": {
      "apiKey": "sk-test", // TODO: use env var
      "baseUrl": "https://api.openai.com/v1",
    },
  },
})";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.agent.model, "gpt-4o");
  EXPECT_EQ(config.agent.max_iterations, 10);
  EXPECT_EQ(config.providers.at("openai").api_key, "sk-test");
}

TEST_F(ConfigTest, Json5UrlsInStringsNotStripped) {
  auto config_path = (test_dir_ / "json5_urls.json").string();
  std::ofstream f(config_path);
  f << R"({
  "providers": {
    "openai": {
      "baseUrl": "https://api.openai.com/v1"
    }
  }
})";
  f.close();

  auto config = quantclaw::QuantClawConfig::LoadFromFile(config_path);
  EXPECT_EQ(config.providers.at("openai").base_url,
            "https://api.openai.com/v1");
}

// --- ModelDefinition / ModelCost / ModelEntryConfig parsing ---

TEST_F(ConfigTest, ModelCostFromJson) {
  nlohmann::json j = {{"input", 0.02},
                      {"output", 0.06},
                      {"cacheRead", 0.001},
                      {"cacheWrite", 0.002}};
  auto cost = quantclaw::ModelCost::FromJson(j);
  EXPECT_DOUBLE_EQ(cost.input, 0.02);
  EXPECT_DOUBLE_EQ(cost.output, 0.06);
  EXPECT_DOUBLE_EQ(cost.cache_read, 0.001);
  EXPECT_DOUBLE_EQ(cost.cache_write, 0.002);
}

TEST_F(ConfigTest, ModelCostDefaults) {
  auto cost = quantclaw::ModelCost::FromJson(nlohmann::json::object());
  EXPECT_DOUBLE_EQ(cost.input, 0.0);
  EXPECT_DOUBLE_EQ(cost.output, 0.0);
  EXPECT_DOUBLE_EQ(cost.cache_read, 0.0);
  EXPECT_DOUBLE_EQ(cost.cache_write, 0.0);
}

TEST_F(ConfigTest, ModelDefinitionFromJson) {
  nlohmann::json j = {{"id", "qwen3-max"},
                      {"name", "Qwen3 Max"},
                      {"reasoning", false},
                      {"input", {"text"}},
                      {"cost", {{"input", 0.02}, {"output", 0.06}}},
                      {"contextWindow", 128000},
                      {"maxTokens", 8192}};
  auto m = quantclaw::ModelDefinition::FromJson(j);
  EXPECT_EQ(m.id, "qwen3-max");
  EXPECT_EQ(m.name, "Qwen3 Max");
  EXPECT_FALSE(m.reasoning);
  ASSERT_EQ(m.input.size(), 1u);
  EXPECT_EQ(m.input[0], "text");
  EXPECT_DOUBLE_EQ(m.cost.input, 0.02);
  EXPECT_DOUBLE_EQ(m.cost.output, 0.06);
  EXPECT_EQ(m.context_window, 128000);
  EXPECT_EQ(m.max_tokens, 8192);
}

TEST_F(ConfigTest, ModelDefinitionWithImageInput) {
  nlohmann::json j = {{"id", "qwen-vl"},
                      {"name", "Qwen VL"},
                      {"input", {"text", "image"}},
                      {"reasoning", true}};
  auto m = quantclaw::ModelDefinition::FromJson(j);
  ASSERT_EQ(m.input.size(), 2u);
  EXPECT_EQ(m.input[1], "image");
  EXPECT_TRUE(m.reasoning);
}

TEST_F(ConfigTest, ModelEntryConfigFromJson) {
  nlohmann::json j = {{"alias", "max"}, {"params", {{"temperature", 0.5}}}};
  auto e = quantclaw::ModelEntryConfig::FromJson(j);
  EXPECT_EQ(e.alias, "max");
  EXPECT_TRUE(e.params.contains("temperature"));
}

TEST_F(ConfigTest, ProviderConfigWithModels) {
  nlohmann::json j = {
      {"apiKey", "test-key"},
      {"baseUrl", "https://example.com/v1"},
      {"api", "openai-completions"},
      {"models",
       {{{"id", "model-a"}, {"name", "Model A"}, {"contextWindow", 32000}},
        {{"id", "model-b"}, {"name", "Model B"}, {"reasoning", true}}}}};
  auto p = quantclaw::ProviderConfig::FromJson(j);
  EXPECT_EQ(p.api_key, "test-key");
  EXPECT_EQ(p.api, "openai-completions");
  ASSERT_EQ(p.models.size(), 2u);
  EXPECT_EQ(p.models[0].id, "model-a");
  EXPECT_EQ(p.models[0].context_window, 32000);
  EXPECT_EQ(p.models[1].id, "model-b");
  EXPECT_TRUE(p.models[1].reasoning);
}

// --- OpenClaw models.providers format ---

TEST_F(ConfigTest, ModelsProvidersSection) {
  nlohmann::json json_config = {
      {"models",
       {{"providers",
         {{"qwen",
           {{"baseUrl", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
            {"apiKey", "test-key"},
            {"api", "openai-completions"},
            {"models",
             {{{"id", "qwen3-max"},
               {"name", "Qwen3 Max"},
               {"contextWindow", 128000},
               {"maxTokens", 8192}},
              {{"id", "qwen-plus"},
               {"name", "Qwen Plus"},
               {"contextWindow", 128000}}}}}}}}}},
      {"gateway", {{"port", 18850}, {"auth", {{"mode", "none"}}}}}};
  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  ASSERT_EQ(config.model_providers.count("qwen"), 1u);
  auto& qwen = config.model_providers.at("qwen");
  EXPECT_EQ(qwen.api_key, "test-key");
  EXPECT_EQ(qwen.api, "openai-completions");
  ASSERT_EQ(qwen.models.size(), 2u);
  EXPECT_EQ(qwen.models[0].id, "qwen3-max");
  EXPECT_EQ(qwen.models[0].context_window, 128000);
  EXPECT_EQ(qwen.models[1].id, "qwen-plus");
}

// --- agents.defaults.models alias parsing ---

TEST_F(ConfigTest, AgentsDefaultsModelsAliases) {
  nlohmann::json json_config = {
      {"agents",
       {{"defaults",
         {{"models",
           {{"qwen/qwen3-max", {{"alias", "max"}}},
            {"qwen/qwen-plus", {{"alias", "plus"}}}}}}}}}};
  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  ASSERT_EQ(config.model_entries.size(), 2u);
  EXPECT_EQ(config.model_entries.at("qwen/qwen3-max").alias, "max");
  EXPECT_EQ(config.model_entries.at("qwen/qwen-plus").alias, "plus");
}

// --- agents.defaults.model object form (primary/fallbacks) ---

TEST_F(ConfigTest, AgentsDefaultsModelObjectForm) {
  nlohmann::json json_config = {
      {"agents",
       {{"defaults",
         {{"model",
           {{"primary", "qwen/qwen3-max"},
            {"fallbacks", {"qwen/qwen-plus", "qwen/qwen-turbo"}}}}}}}}};
  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  EXPECT_EQ(config.agent.model, "qwen/qwen3-max");
  ASSERT_EQ(config.agent.fallbacks.size(), 2u);
  EXPECT_EQ(config.agent.fallbacks[0], "qwen/qwen-plus");
  EXPECT_EQ(config.agent.fallbacks[1], "qwen/qwen-turbo");
}

// --- Combined OpenClaw config test ---

TEST_F(ConfigTest, FullOpenClawMultiModelConfig) {
  nlohmann::json json_config = {
      {"models",
       {{"providers",
         {{"qwen",
           {{"baseUrl", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
            {"apiKey", "test"},
            {"api", "openai-completions"},
            {"models",
             {{{"id", "qwen3-max"},
               {"name", "Qwen3 Max"},
               {"reasoning", false},
               {"input", {"text"}},
               {"cost", {{"input", 0.02}, {"output", 0.06}}},
               {"contextWindow", 128000},
               {"maxTokens", 8192}}}}}}}}}},
      {"agents",
       {{"defaults",
         {{"model",
           {{"primary", "qwen/qwen3-max"}, {"fallbacks", {"qwen/qwen-plus"}}}},
          {"models",
           {{"qwen/qwen3-max", {{"alias", "max"}}},
            {"qwen/qwen-plus", {{"alias", "plus"}}}}}}}}},
      {"gateway", {{"port", 18850}, {"auth", {{"mode", "none"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);

  // Model providers
  ASSERT_EQ(config.model_providers.count("qwen"), 1u);
  EXPECT_EQ(config.model_providers.at("qwen").models[0].id, "qwen3-max");

  // Agent model (from object form)
  EXPECT_EQ(config.agent.model, "qwen/qwen3-max");
  ASSERT_EQ(config.agent.fallbacks.size(), 1u);
  EXPECT_EQ(config.agent.fallbacks[0], "qwen/qwen-plus");

  // Aliases
  EXPECT_EQ(config.model_entries.at("qwen/qwen3-max").alias, "max");
  EXPECT_EQ(config.model_entries.at("qwen/qwen-plus").alias, "plus");

  // Gateway
  EXPECT_EQ(config.gateway.port, 18850);
  EXPECT_EQ(config.gateway.auth.mode, "none");
}

// --- Model catalog generation ---

TEST_F(ConfigTest, ModelCatalogFromProviderRegistry) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("catalog_test", null_sink);

  quantclaw::ProviderRegistry registry(logger);
  registry.RegisterBuiltinFactories();

  std::unordered_map<std::string, quantclaw::ProviderConfig> model_providers;
  quantclaw::ProviderConfig prov;
  prov.api_key = "test";
  prov.base_url = "https://example.com/v1";
  prov.api = "openai-completions";
  prov.models.push_back(
      quantclaw::ModelDefinition::FromJson({{"id", "model-a"},
                                            {"name", "Model A"},
                                            {"contextWindow", 128000},
                                            {"reasoning", true}}));
  prov.models.push_back(quantclaw::ModelDefinition::FromJson(
      {{"id", "model-b"}, {"name", "Model B"}, {"contextWindow", 32000}}));
  model_providers["test-provider"] = prov;

  registry.LoadModelProviders(model_providers);

  auto catalog = registry.GetModelCatalog();
  ASSERT_EQ(catalog.size(), 2u);
  EXPECT_EQ(catalog[0].id, "model-a");
  EXPECT_EQ(catalog[0].provider, "test-provider");
  EXPECT_EQ(catalog[0].context_window, 128000);
  EXPECT_TRUE(catalog[0].reasoning);
  EXPECT_EQ(catalog[1].id, "model-b");
}

TEST_F(ConfigTest, ModelCatalogEntryToJson) {
  quantclaw::ProviderRegistry::ModelCatalogEntry ce;
  ce.id = "test-model";
  ce.name = "Test Model";
  ce.provider = "test";
  ce.context_window = 64000;
  ce.reasoning = true;
  ce.input = {"text", "image"};
  ce.max_tokens = 4096;
  ce.cost.input = 0.01;
  ce.cost.output = 0.03;

  auto j = ce.ToJson();
  EXPECT_EQ(j["id"], "test-model");
  EXPECT_EQ(j["name"], "Test Model");
  EXPECT_EQ(j["provider"], "test");
  EXPECT_EQ(j["contextWindow"], 64000);
  EXPECT_TRUE(j["reasoning"].get<bool>());
  ASSERT_EQ(j["input"].size(), 2u);
  EXPECT_EQ(j["maxTokens"], 4096);
  EXPECT_DOUBLE_EQ(j["cost"]["input"].get<double>(), 0.01);
}

TEST_F(ConfigTest, LoadModelProvidersMergesWithExisting) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("merge_test", null_sink);

  quantclaw::ProviderRegistry registry(logger);
  registry.RegisterBuiltinFactories();

  // Add an existing provider entry
  quantclaw::ProviderEntry existing;
  existing.id = "qwen";
  existing.api_key = "existing-key";
  existing.base_url = "https://existing.com/v1";
  registry.AddProvider(existing);

  // Load model providers that overlap
  std::unordered_map<std::string, quantclaw::ProviderConfig> model_providers;
  quantclaw::ProviderConfig prov;
  prov.api = "openai-completions";
  prov.models.push_back(quantclaw::ModelDefinition::FromJson(
      {{"id", "qwen3-max"}, {"name", "Qwen3 Max"}}));
  model_providers["qwen"] = prov;

  registry.LoadModelProviders(model_providers);

  // Should merge: existing api_key preserved, models added
  auto* entry = registry.GetEntry("qwen");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->api_key, "existing-key");
  EXPECT_EQ(entry->api, "openai-completions");
  ASSERT_EQ(entry->models.size(), 1u);
  EXPECT_EQ(entry->models[0].id, "qwen3-max");
}

TEST_F(ConfigTest, EnvVarNoSubstitutionWithoutDollarBrace) {
  nlohmann::json json_config = {
      {"providers", {{"openai", {{"apiKey", "literal-string-no-vars"}}}}}};

  auto config = quantclaw::QuantClawConfig::FromJson(json_config);
  EXPECT_EQ(config.providers.at("openai").api_key, "literal-string-no-vars");
}
