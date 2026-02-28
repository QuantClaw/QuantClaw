// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <memory>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>

#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/config.hpp"
#include "quantclaw/core/skill_loader.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

// Forward declare
namespace quantclaw {
    class ProviderRegistry;
    class SkillLoader;
    class CronScheduler;
    class ExecApprovalManager;
}
namespace quantclaw::gateway {
    void register_rpc_handlers(
        GatewayServer& server,
        std::shared_ptr<quantclaw::SessionManager> session_manager,
        std::shared_ptr<quantclaw::AgentLoop> agent_loop,
        std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
        std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
        const quantclaw::QuantClawConfig& config,
        std::shared_ptr<spdlog::logger> logger,
        std::function<void()> reload_fn = nullptr,
        std::shared_ptr<quantclaw::ProviderRegistry> provider_registry = nullptr,
        std::shared_ptr<quantclaw::SkillLoader> skill_loader = nullptr,
        std::shared_ptr<quantclaw::CronScheduler> cron_scheduler = nullptr,
        std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr = nullptr);
}

// Minimal mock LLM
class RpcMockLLMProvider : public quantclaw::LLMProvider {
public:
    quantclaw::ChatCompletionResponse ChatCompletion(const quantclaw::ChatCompletionRequest&) override {
        quantclaw::ChatCompletionResponse resp;
        resp.content = "mock reply";
        resp.finish_reason = "stop";
        return resp;
    }

    void ChatCompletionStream(const quantclaw::ChatCompletionRequest&,
                                std::function<void(const quantclaw::ChatCompletionResponse&)> callback) override {
        quantclaw::ChatCompletionResponse delta;
        delta.content = "mock reply";
        callback(delta);

        quantclaw::ChatCompletionResponse end;
        end.content = "mock reply";
        end.is_stream_end = true;
        callback(end);
    }

    std::string GetProviderName() const override { return "rpc-mock"; }
    std::vector<std::string> GetSupportedModels() const override { return {"mock"}; }
};

class RpcHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "quantclaw_rpc_test";
        workspace_dir_ = test_dir_ / "workspace";
        sessions_dir_ = test_dir_ / "sessions";
        std::filesystem::create_directories(workspace_dir_);
        std::filesystem::create_directories(sessions_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("rpc_test", null_sink);

        config_.agent.model = "mock-model";
        config_.agent.max_iterations = 3;
        config_.agent.temperature = 0.0;
        config_.agent.max_tokens = 512;
        config_.gateway.port = port_;
        config_.gateway.auth.mode = "none";

        memory_manager_ = std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
        skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
        tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
        tool_registry_->RegisterBuiltinTools();
        tool_registry_->RegisterChainTool();

        mock_llm_ = std::make_shared<RpcMockLLMProvider>();
        agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
            memory_manager_, skill_loader_, tool_registry_, mock_llm_, config_.agent, logger_);
        session_manager_ = std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
        prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
            memory_manager_, skill_loader_, tool_registry_);

        server_ = std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
        server_->SetAuth("none", "");

        quantclaw::gateway::register_rpc_handlers(
            *server_, session_manager_, agent_loop_, prompt_builder_, tool_registry_, config_, logger_);

        server_->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_) { server_->Stop(); server_.reset(); }
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    std::unique_ptr<quantclaw::gateway::GatewayClient> make_client() {
        std::string url = "ws://127.0.0.1:" + std::to_string(port_);
        return std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    }

    static int next_port() {
        static std::atomic<int> p{33000};
        return p++;
    }

    int port_ = next_port();
    std::filesystem::path test_dir_;
    std::filesystem::path workspace_dir_;
    std::filesystem::path sessions_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    quantclaw::QuantClawConfig config_;
    std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
    std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
    std::shared_ptr<RpcMockLLMProvider> mock_llm_;
    std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
    std::shared_ptr<quantclaw::SessionManager> session_manager_;
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
    std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
};

// --- config.get edge cases ---

TEST_F(RpcHandlersTest, ConfigGetUnknownPath) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    // Unknown config path should error
    EXPECT_THROW(client->Call("config.get", {{"path", "nonexistent.key"}}, 5000), std::runtime_error);

    client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetGatewayPort) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("config.get", {{"path", "gateway.port"}});
    EXPECT_EQ(result.get<int>(), port_);

    client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetGatewayBind) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("config.get", {{"path", "gateway.bind"}});
    EXPECT_FALSE(result.get<std::string>().empty());

    client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetAgentTemperature) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("config.get", {{"path", "agent.temperature"}});
    EXPECT_DOUBLE_EQ(result.get<double>(), 0.0);

    client->Disconnect();
}

TEST_F(RpcHandlersTest, ConfigGetAgentMaxIterations) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("config.get", {{"path", "agent.maxIterations"}});
    EXPECT_EQ(result.get<int>(), 3);

    client->Disconnect();
}

// --- agent.request edge cases ---

TEST_F(RpcHandlersTest, AgentRequestEmptyMessage) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    EXPECT_THROW(client->Call("agent.request", {{"message", ""}}, 5000), std::runtime_error);

    client->Disconnect();
}

TEST_F(RpcHandlersTest, AgentRequestCustomSessionKey) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("agent.request", {
        {"message", "Hello"},
        {"sessionKey", "custom:session:key"}
    }, 10000);

    EXPECT_EQ(result["sessionKey"], "custom:session:key");

    client->Disconnect();
}

// --- sessions.history edge cases ---

TEST_F(RpcHandlersTest, SessionsHistoryMissingKey) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    EXPECT_THROW(client->Call("sessions.history", nlohmann::json::object(), 5000), std::runtime_error);

    client->Disconnect();
}

// --- sessions.list pagination ---

TEST_F(RpcHandlersTest, SessionsListEmptyInitially) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("sessions.list", nlohmann::json::object());
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 0u);

    client->Disconnect();
}

TEST_F(RpcHandlersTest, SessionsListWithLimitOffset) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    // Create two sessions
    client->Call("agent.request", {{"message", "Hi"}, {"sessionKey", "a:1:main"}}, 10000);
    client->Call("agent.request", {{"message", "Hello"}, {"sessionKey", "b:2:main"}}, 10000);

    // List all
    auto all = client->Call("sessions.list", {{"limit", 50}, {"offset", 0}});
    ASSERT_TRUE(all.is_array());
    EXPECT_GE(all.size(), 2u);

    // List with limit=1
    auto limited = client->Call("sessions.list", {{"limit", 1}, {"offset", 0}});
    ASSERT_TRUE(limited.is_array());
    EXPECT_EQ(limited.size(), 1u);

    // List with offset past all sessions
    auto empty = client->Call("sessions.list", {{"limit", 10}, {"offset", 100}});
    ASSERT_TRUE(empty.is_array());
    EXPECT_EQ(empty.size(), 0u);

    client->Disconnect();
}

// --- health returns uptime and version ---

TEST_F(RpcHandlersTest, HealthContainsUptime) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("gateway.health");
    EXPECT_TRUE(result.contains("uptime"));
    EXPECT_GE(result["uptime"].get<int>(), 0);

    client->Disconnect();
}

// --- status shows port ---

TEST_F(RpcHandlersTest, StatusShowsPort) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("gateway.status");
    EXPECT_EQ(result["port"].get<int>(), port_);

    client->Disconnect();
}

// --- config.reload RPC test ---

class RpcReloadTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "quantclaw_rpc_reload_test";
        workspace_dir_ = test_dir_ / "workspace";
        sessions_dir_ = test_dir_ / "sessions";
        std::filesystem::create_directories(workspace_dir_);
        std::filesystem::create_directories(sessions_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("rpc_reload_test", null_sink);

        config_.agent.model = "mock-model";
        config_.agent.max_iterations = 3;
        config_.agent.temperature = 0.0;
        config_.agent.max_tokens = 512;
        config_.gateway.port = port_;
        config_.gateway.auth.mode = "none";

        memory_manager_ = std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
        skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
        tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
        tool_registry_->RegisterBuiltinTools();
        tool_registry_->RegisterChainTool();

        mock_llm_ = std::make_shared<RpcMockLLMProvider>();
        agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
            memory_manager_, skill_loader_, tool_registry_, mock_llm_, config_.agent, logger_);
        session_manager_ = std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
        prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
            memory_manager_, skill_loader_, tool_registry_);

        reload_called_ = false;
        reload_fn_ = [this]() { reload_called_ = true; };

        server_ = std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
        server_->SetAuth("none", "");

        quantclaw::gateway::register_rpc_handlers(
            *server_, session_manager_, agent_loop_, prompt_builder_, tool_registry_, config_, logger_, reload_fn_);

        server_->Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_) { server_->Stop(); server_.reset(); }
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    std::unique_ptr<quantclaw::gateway::GatewayClient> make_client() {
        std::string url = "ws://127.0.0.1:" + std::to_string(port_);
        return std::make_unique<quantclaw::gateway::GatewayClient>(url, "", logger_);
    }

    static int next_port() {
        static std::atomic<int> p{34000};
        return p++;
    }

    int port_ = next_port();
    std::filesystem::path test_dir_;
    std::filesystem::path workspace_dir_;
    std::filesystem::path sessions_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    quantclaw::QuantClawConfig config_;
    std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
    std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
    std::shared_ptr<RpcMockLLMProvider> mock_llm_;
    std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
    std::shared_ptr<quantclaw::SessionManager> session_manager_;
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
    std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
    std::function<void()> reload_fn_;
    bool reload_called_;
};

TEST_F(RpcReloadTest, ConfigReloadRPC) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    EXPECT_FALSE(reload_called_);

    auto result = client->Call("config.reload", {});
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(reload_called_);

    client->Disconnect();
}

// ================================================================
// OpenClaw protocol compatibility tests
// ================================================================

// Test that OpenClaw-style "connect" method with nested params returns capabilities
TEST_F(RpcHandlersTest, OpenClawConnect) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    // Send OpenClaw-style connect with nested params
    auto result = client->Call("connect", {
        {"client", {{"name", "openclaw-test"}, {"version", "1.0.0"}}},
        {"auth", {{"token", ""}}},
        {"device", {{"id", "test-device"}}}
    }, 5000);

    // Verify OpenClaw hello-ok response contains capabilities
    EXPECT_TRUE(result.contains("capabilities"));
    EXPECT_TRUE(result["capabilities"].is_array());
    EXPECT_TRUE(result.contains("authenticated"));
    EXPECT_EQ(result["authenticated"], true);
    EXPECT_TRUE(result.contains("protocol"));

    client->Disconnect();
}

// Test chat.send streaming with OpenClaw event format
TEST_F(RpcHandlersTest, ChatSendStreaming) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    // Subscribe to OpenClaw events
    std::atomic<int> agent_events{0};
    std::atomic<int> chat_events{0};
    client->Subscribe("agent", [&agent_events](const std::string&, const nlohmann::json& payload) {
        agent_events++;
    });
    client->Subscribe("chat", [&chat_events](const std::string&, const nlohmann::json& payload) {
        // Verify chat event has "state":"final"
        if (payload.contains("state") && payload["state"] == "final") {
            chat_events++;
        }
    });

    auto result = client->Call("chat.send", {
        {"message", "Hello from OpenClaw"},
        {"sessionKey", "oc:chat:test"}
    }, 10000);

    EXPECT_EQ(result["sessionKey"], "oc:chat:test");
    EXPECT_FALSE(result["response"].get<std::string>().empty());

    // Give events a moment to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // At minimum we should get a chat final event (the mock sends text + end)
    EXPECT_GE(chat_events.load(), 1);

    client->Disconnect();
}

// Test chat.history alias
TEST_F(RpcHandlersTest, ChatHistoryAlias) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    // Create a session with a message first
    client->Call("agent.request", {
        {"message", "Hi"},
        {"sessionKey", "oc:hist:test"}
    }, 10000);

    // Call chat.history (OpenClaw alias for sessions.history)
    auto result = client->Call("chat.history", {{"sessionKey", "oc:hist:test"}});

    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 1u);

    client->Disconnect();
}

// Test models.list stub
TEST_F(RpcHandlersTest, ModelsListStub) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("models.list");

    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 1u);
    EXPECT_TRUE(result[0].contains("id"));
    EXPECT_TRUE(result[0].contains("provider"));
    EXPECT_TRUE(result[0].contains("active"));
    EXPECT_EQ(result[0]["active"], true);

    client->Disconnect();
}

// Test tools.catalog
TEST_F(RpcHandlersTest, ToolsCatalogStub) {
    auto client = make_client();
    ASSERT_TRUE(client->Connect(5000));

    auto result = client->Call("tools.catalog");

    ASSERT_TRUE(result.is_array());
    EXPECT_GE(result.size(), 1u);  // Should have builtin tools

    // Verify schema structure
    for (const auto& tool : result) {
        EXPECT_TRUE(tool.contains("name"));
        EXPECT_TRUE(tool.contains("description"));
        EXPECT_TRUE(tool.contains("parameters"));
    }

    client->Disconnect();
}
