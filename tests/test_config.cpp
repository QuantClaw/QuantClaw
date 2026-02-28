#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "quantclaw_config_test";
        std::filesystem::create_directories(test_dir_);
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
    nlohmann::json json_config = {
        {"agents", {
            {"defaults", {
                {"model", "gpt-4-turbo"},
                {"max_iterations", 15},
                {"temperature", 0.7},
                {"max_tokens", 4096}
            }}
        }},
        {"providers", {
            {"openai", {
                {"api_key", "test-key"},
                {"base_url", "https://api.openai.com/v1"},
                {"timeout", 30}
            }}
        }},
        {"channels", {
            {"discord", {
                {"enabled", true},
                {"token", "test-token"},
                {"allowed_ids", {"123", "456"}}
            }}
        }},
        {"tools", {
            {"filesystem", {
                {"enabled", true},
                {"allowed_paths", {"./workspace"}},
                {"denied_paths", {"/etc", "/sys"}}
            }}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);

    EXPECT_EQ(config.agent.model, "gpt-4-turbo");
    EXPECT_EQ(config.agent.max_iterations, 15);
    EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
    EXPECT_EQ(config.agent.max_tokens, 4096);

    ASSERT_TRUE(config.providers.count("openai"));
    EXPECT_EQ(config.providers.at("openai").api_key, "test-key");
    EXPECT_EQ(config.providers.at("openai").base_url, "https://api.openai.com/v1");
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
        {"agent", {
            {"model", "anthropic/claude-sonnet-4-6"},
            {"maxIterations", 15},
            {"temperature", 0.7}
        }},
        {"gateway", {
            {"port", 18789},
            {"bind", "loopback"},
            {"auth", {{"mode", "token"}}},
            {"controlUi", {{"enabled", true}}}
        }},
        {"providers", {
            {"openai", {
                {"apiKey", "sk-test"},
                {"baseUrl", "https://api.openai.com/v1"}
            }}
        }},
        {"tools", {
            {"allow", {"group:fs", "group:runtime"}},
            {"deny", nlohmann::json::array()}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);

    EXPECT_EQ(config.agent.model, "anthropic/claude-sonnet-4-6");
    EXPECT_EQ(config.agent.max_iterations, 15);

    EXPECT_EQ(config.gateway.port, 18789);
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
    auto config = quantclaw::QuantClawConfig::from_json({});

    EXPECT_EQ(config.agent.model, "qwen-max");
    EXPECT_EQ(config.agent.max_iterations, 15);
    EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
    EXPECT_EQ(config.agent.max_tokens, 4096);

    EXPECT_EQ(config.gateway.port, 18789);
    EXPECT_EQ(config.gateway.bind, "loopback");
}

TEST_F(ConfigTest, PartialAgentConfig) {
    nlohmann::json json_config = {
        {"agent", {
            {"model", "gpt-3.5-turbo"}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);

    EXPECT_EQ(config.agent.model, "gpt-3.5-turbo");
    EXPECT_EQ(config.agent.max_iterations, 15);
    EXPECT_DOUBLE_EQ(config.agent.temperature, 0.7);
}

// --- Gateway config ---

TEST_F(ConfigTest, GatewayConfigDefaults) {
    quantclaw::GatewayConfig gw;
    EXPECT_EQ(gw.port, 18789);
    EXPECT_EQ(gw.bind, "loopback");
    EXPECT_EQ(gw.auth.mode, "token");
    EXPECT_TRUE(gw.control_ui.enabled);
}

TEST_F(ConfigTest, GatewayConfigFromJson) {
    nlohmann::json j = {
        {"port", 9999},
        {"bind", "0.0.0.0"},
        {"auth", {{"mode", "none"}}},
        {"controlUi", {{"enabled", false}}}
    };

    auto gw = quantclaw::GatewayConfig::from_json(j);
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

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path.string());
    EXPECT_EQ(config.agent.model, "test-model");
    EXPECT_EQ(config.gateway.port, 12345);
}

TEST_F(ConfigTest, LoadFromMissingFile) {
    EXPECT_THROW(
        quantclaw::QuantClawConfig::load_from_file("/nonexistent/config.json"),
        std::runtime_error
    );
}

// --- Helper methods ---

TEST_F(ConfigTest, ExpandHome) {
    std::string path = "~/test/path";
    std::string expanded = quantclaw::QuantClawConfig::expand_home(path);

    EXPECT_NE(expanded.substr(0, 2), "~/");
    EXPECT_TRUE(expanded.find("/test/path") != std::string::npos);
}

TEST_F(ConfigTest, DefaultConfigPath) {
    std::string path = quantclaw::QuantClawConfig::default_config_path();
    EXPECT_TRUE(path.find(".quantclaw/quantclaw.json") != std::string::npos);
}

// --- Auth token parsing ---

TEST_F(ConfigTest, GatewayAuthTokenFromJson) {
    nlohmann::json j = {
        {"mode", "token"},
        {"token", "secret-auth-token-123"}
    };

    auto auth = quantclaw::GatewayAuthConfig::from_json(j);
    EXPECT_EQ(auth.mode, "token");
    EXPECT_EQ(auth.token, "secret-auth-token-123");
}

TEST_F(ConfigTest, GatewayAuthTokenDefaultsEmpty) {
    nlohmann::json j = {{"mode", "token"}};

    auto auth = quantclaw::GatewayAuthConfig::from_json(j);
    EXPECT_EQ(auth.mode, "token");
    EXPECT_TRUE(auth.token.empty());
}

TEST_F(ConfigTest, GatewayAuthNoneMode) {
    nlohmann::json j = {{"mode", "none"}};

    auto auth = quantclaw::GatewayAuthConfig::from_json(j);
    EXPECT_EQ(auth.mode, "none");
}

TEST_F(ConfigTest, FullConfigWithAuthToken) {
    nlohmann::json json_config = {
        {"gateway", {
            {"port", 18789},
            {"auth", {{"mode", "token"}, {"token", "my-secret"}}}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);
    EXPECT_EQ(config.gateway.auth.mode, "token");
    EXPECT_EQ(config.gateway.auth.token, "my-secret");
}

TEST_F(ConfigTest, MCPConfigParsing) {
    nlohmann::json json_config = {
        {"mcp", {
            {"servers", {
                {{"name", "test-server"}, {"url", "http://localhost:3000"}, {"timeout", 60}}
            }}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);
    ASSERT_EQ(config.mcp.servers.size(), 1u);
    EXPECT_EQ(config.mcp.servers[0].name, "test-server");
    EXPECT_EQ(config.mcp.servers[0].url, "http://localhost:3000");
    EXPECT_EQ(config.mcp.servers[0].timeout, 60);
}

// --- Skills config ---

TEST_F(ConfigTest, SkillsConfigParsing) {
    nlohmann::json json_config = {
        {"skills", {
            {"load", {{"extraDirs", {"/path/to/skills", "/another/path"}}}},
            {"entries", {
                {"discord", {{"enabled", false}}},
                {"weather", {{"enabled", true}}}
            }}
        }}
    };

    auto config = quantclaw::QuantClawConfig::from_json(json_config);

    ASSERT_EQ(config.skills.load.extra_dirs.size(), 2u);
    EXPECT_EQ(config.skills.load.extra_dirs[0], "/path/to/skills");
    EXPECT_EQ(config.skills.load.extra_dirs[1], "/another/path");

    ASSERT_TRUE(config.skills.entries.count("discord"));
    EXPECT_FALSE(config.skills.entries.at("discord").enabled);

    ASSERT_TRUE(config.skills.entries.count("weather"));
    EXPECT_TRUE(config.skills.entries.at("weather").enabled);
}

TEST_F(ConfigTest, SkillsConfigDefaults) {
    auto config = quantclaw::QuantClawConfig::from_json({});

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

    // Wait and modify
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::ofstream f(config_path);
        f << R"({"agent": {"model": "updated-model"}})";
    }

    auto mtime2 = std::filesystem::last_write_time(config_path);

    EXPECT_NE(mtime1, mtime2);

    // Verify the updated content loads correctly
    auto config = quantclaw::QuantClawConfig::load_from_file(config_path.string());
    EXPECT_EQ(config.agent.model, "updated-model");
}

// --- Config reload propagates to AgentLoop ---

// Minimal mock for this test
class ConfigReloadMockLLM : public quantclaw::LLMProvider {
public:
    quantclaw::ChatCompletionResponse chat_completion(const quantclaw::ChatCompletionRequest&) override {
        quantclaw::ChatCompletionResponse resp;
        resp.content = "mock";
        resp.finish_reason = "stop";
        return resp;
    }
    void chat_completion_stream(const quantclaw::ChatCompletionRequest&,
                                std::function<void(const quantclaw::ChatCompletionResponse&)> cb) override {
        quantclaw::ChatCompletionResponse end;
        end.content = "mock";
        end.is_stream_end = true;
        cb(end);
    }
    std::string get_provider_name() const override { return "config-mock"; }
    std::vector<std::string> get_supported_models() const override { return {"mock"}; }
};

TEST_F(ConfigTest, ConfigReload_PropagatesChanges) {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("config_reload_test", null_sink);

    auto workspace_dir = test_dir_ / "workspace";
    std::filesystem::create_directories(workspace_dir);

    auto memory_manager = std::make_shared<quantclaw::MemoryManager>(workspace_dir, logger);
    auto skill_loader = std::make_shared<quantclaw::SkillLoader>(logger);
    auto tool_registry = std::make_shared<quantclaw::ToolRegistry>(logger);
    auto mock_llm = std::make_shared<ConfigReloadMockLLM>();

    quantclaw::AgentConfig initial_config;
    initial_config.model = "initial-model";
    initial_config.max_iterations = 5;
    initial_config.temperature = 0.5;

    auto agent_loop = std::make_shared<quantclaw::AgentLoop>(
        memory_manager, skill_loader, tool_registry, mock_llm, initial_config, logger);

    EXPECT_EQ(agent_loop->get_config().model, "initial-model");
    EXPECT_EQ(agent_loop->get_config().max_iterations, 5);

    // Simulate reload: set new config
    quantclaw::AgentConfig new_config;
    new_config.model = "reloaded-model";
    new_config.max_iterations = 20;
    new_config.temperature = 0.9;

    agent_loop->set_config(new_config);

    EXPECT_EQ(agent_loop->get_config().model, "reloaded-model");
    EXPECT_EQ(agent_loop->get_config().max_iterations, 20);
    EXPECT_DOUBLE_EQ(agent_loop->get_config().temperature, 0.9);
}

// --- Config set_value / unset_value ---

TEST_F(ConfigTest, SetValue_CreatesNewKey) {
    auto config_path = (test_dir_ / "set_test.json").string();

    // Start with empty config
    {
        std::ofstream f(config_path);
        f << "{}";
    }

    quantclaw::QuantClawConfig::set_value(config_path, "agent.model", "gpt-4o");

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.agent.model, "gpt-4o");
}

TEST_F(ConfigTest, SetValue_OverwritesExisting) {
    auto config_path = (test_dir_ / "set_overwrite.json").string();

    {
        std::ofstream f(config_path);
        f << R"({"agent": {"model": "old-model", "temperature": 0.5}})";
    }

    quantclaw::QuantClawConfig::set_value(config_path, "agent.model", "new-model");

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.agent.model, "new-model");
    EXPECT_DOUBLE_EQ(config.agent.temperature, 0.5);  // Other fields preserved
}

TEST_F(ConfigTest, SetValue_CreatesIntermediateObjects) {
    auto config_path = (test_dir_ / "set_nested.json").string();

    {
        std::ofstream f(config_path);
        f << "{}";
    }

    quantclaw::QuantClawConfig::set_value(config_path, "gateway.auth.token", "my-secret");

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.gateway.auth.token, "my-secret");
}

TEST_F(ConfigTest, SetValue_CreatesBackup) {
    auto config_path = (test_dir_ / "set_backup.json").string();
    auto backup_path = config_path + ".bak";

    {
        std::ofstream f(config_path);
        f << R"({"agent": {"model": "original"}})";
    }

    quantclaw::QuantClawConfig::set_value(config_path, "agent.model", "changed");

    EXPECT_TRUE(std::filesystem::exists(backup_path));
    auto backup = quantclaw::QuantClawConfig::load_from_file(backup_path);
    EXPECT_EQ(backup.agent.model, "original");
}

TEST_F(ConfigTest, SetValue_NumericValue) {
    auto config_path = (test_dir_ / "set_numeric.json").string();

    {
        std::ofstream f(config_path);
        f << "{}";
    }

    quantclaw::QuantClawConfig::set_value(config_path, "gateway.port", 9999);

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.gateway.port, 9999);
}

TEST_F(ConfigTest, UnsetValue_RemovesKey) {
    auto config_path = (test_dir_ / "unset_test.json").string();

    {
        std::ofstream f(config_path);
        f << R"({"agent": {"model": "gpt-4", "temperature": 0.5}})";
    }

    quantclaw::QuantClawConfig::unset_value(config_path, "agent.temperature");

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
    EXPECT_NO_THROW(
        quantclaw::QuantClawConfig::unset_value(config_path, "nonexistent.deep.path")
    );

    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.agent.model, "gpt-4");
}

TEST_F(ConfigTest, SetValue_OnNonexistentFile) {
    auto config_path = (test_dir_ / "new_config.json").string();

    // File doesn't exist yet
    EXPECT_FALSE(std::filesystem::exists(config_path));

    quantclaw::QuantClawConfig::set_value(config_path, "agent.model", "gpt-4o");

    EXPECT_TRUE(std::filesystem::exists(config_path));
    auto config = quantclaw::QuantClawConfig::load_from_file(config_path);
    EXPECT_EQ(config.agent.model, "gpt-4o");
}
