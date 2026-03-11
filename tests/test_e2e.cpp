// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Forward declare register_rpc_handlers
namespace quantclaw {
class ProviderRegistry;
class SkillLoader;
class CronScheduler;
class ExecApprovalManager;
class PluginSystem;
}  // namespace quantclaw
namespace quantclaw::gateway {
class CommandQueue;
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
    std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr = nullptr,
    quantclaw::PluginSystem* plugin_system = nullptr,
    quantclaw::gateway::CommandQueue* command_queue = nullptr,
    std::string log_file_path = {});
}  // namespace quantclaw::gateway

// --- Mock LLM Provider ---

class E2EMockLLMProvider : public quantclaw::LLMProvider {
 public:
  std::string response_text = "Hello from QuantClaw E2E mock.";

  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest& /*request*/) override {
    quantclaw::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest& /*request*/,
      std::function<void(const quantclaw::ChatCompletionResponse&)> callback)
      override {
    // Emit a text delta then stream end
    quantclaw::ChatCompletionResponse delta;
    delta.content = response_text;
    delta.is_stream_end = false;
    callback(delta);

    quantclaw::ChatCompletionResponse end;
    end.content = response_text;
    end.is_stream_end = true;
    end.finish_reason = "stop";
    callback(end);
  }

  std::string GetProviderName() const override {
    return "e2e-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"e2e-mock-model"};
  }
};

// --- Test fixture: full in-process gateway ---

class E2ETest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Temp directories
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_e2e_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    // Create a test file for read_file tool
    {
      std::ofstream f(workspace_dir_ / "hello.txt");
      f << "hello world";
    }

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("e2e", null_sink);

    // Build config
    config_.agent.model = "e2e-mock-model";
    config_.agent.max_iterations = 5;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 1024;
    config_.gateway.port = port_;
    config_.gateway.auth.mode = "none";
    config_.gateway.auth.token = "";

    // Initialize components (mirrors gateway_commands.cpp)
    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();
    tool_registry_->RegisterChainTool();

    mock_llm_ = std::make_shared<E2EMockLLMProvider>();

    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);

    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);

    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    // Create server
    server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
    server_->SetAuth(config_.gateway.auth.mode, config_.gateway.auth.token);

    // Register RPC handlers
    quantclaw::gateway::register_rpc_handlers(*server_, session_manager_,
                                              agent_loop_, prompt_builder_,
                                              tool_registry_, config_, logger_);

    // Start server
    server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
      server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  // Helper: create a connected client (auth=none)
  std::unique_ptr<quantclaw::gateway::GatewayClient>
  make_client(const std::string& token = "") {
    std::string url = "ws://127.0.0.1:" + std::to_string(port_);
    auto client = std::make_unique<quantclaw::gateway::GatewayClient>(
        url, token, logger_);
    return client;
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
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
  std::shared_ptr<E2EMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
};

// ======================================================================
// E2E Tests
// ======================================================================

TEST_F(E2ETest, E2E_HealthRpc) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("gateway.health");
  EXPECT_EQ(result["status"], "ok");
  EXPECT_TRUE(result.contains("version"));

  client->Disconnect();
}

TEST_F(E2ETest, E2E_StatusRpc) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("gateway.status");
  EXPECT_TRUE(result["running"].get<bool>());
  EXPECT_TRUE(result.contains("connections"));
  EXPECT_TRUE(result.contains("sessions"));
  EXPECT_TRUE(result.contains("uptime"));

  client->Disconnect();
}

TEST_F(E2ETest, E2E_ConfigGet) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", nlohmann::json::object());
  // Full config now returns ConfigSnapshot {path, exists, raw, hash, valid,
  // config, issues}
  EXPECT_TRUE(result.contains("config"));
  EXPECT_TRUE(result.contains("valid"));
  EXPECT_TRUE(result["config"].contains("agent"));
  EXPECT_TRUE(result["config"].contains("gateway"));
  EXPECT_TRUE(result["config"]["agent"].contains("model"));
  EXPECT_TRUE(result["config"]["gateway"].contains("port"));

  client->Disconnect();
}

TEST_F(E2ETest, E2E_ConfigGetPath) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("config.get", {{"path", "agent.model"}});
  // Dot-path should return the model string directly
  EXPECT_EQ(result.get<std::string>(), "e2e-mock-model");

  client->Disconnect();
}

TEST_F(E2ETest, E2E_AgentRequest) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Subscribe to streaming events
  std::atomic<bool> got_text_delta{false};
  std::atomic<bool> got_message_end{false};

  client->Subscribe("agent.text_delta",
                    [&](const std::string&, const nlohmann::json&) {
                      got_text_delta = true;
                    });
  client->Subscribe("agent.message_end",
                    [&](const std::string&, const nlohmann::json&) {
                      got_message_end = true;
                    });

  auto result = client->Call("agent.request", {{"message", "Hello"}}, 10000);
  EXPECT_TRUE(result.contains("sessionKey"));
  EXPECT_TRUE(result.contains("response"));
  EXPECT_FALSE(result["response"].get<std::string>().empty());

  // Give events time to arrive
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_TRUE(got_message_end);

  client->Disconnect();
}

TEST_F(E2ETest, E2E_AgentStop) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("agent.stop");
  EXPECT_TRUE(result.contains("ok"));
  EXPECT_TRUE(result["ok"].get<bool>());

  client->Disconnect();
}

TEST_F(E2ETest, E2E_SessionsAfterRequest) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  // Send a request to create a session
  client->Call("agent.request", {{"message", "Hi"}}, 10000);

  auto sessions_res = client->Call("sessions.list", nlohmann::json::object());
  // New shape: {ts, path, count, defaults, sessions:[...]}
  ASSERT_TRUE(sessions_res.is_object());
  ASSERT_TRUE(sessions_res.contains("sessions"));
  auto& sessions = sessions_res["sessions"];
  ASSERT_TRUE(sessions.is_array());
  EXPECT_GE(sessions.size(), 1u);

  // Verify session has expected fields
  auto& s = sessions[0];
  EXPECT_TRUE(s.contains("key"));
  EXPECT_TRUE(s.contains("sessionId"));

  client->Disconnect();
}

TEST_F(E2ETest, E2E_SessionHistory) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  std::string session_key = "agent:main:main";
  client->Call("agent.request",
               {{"message", "Tell me something"}, {"sessionKey", session_key}},
               10000);

  auto history =
      client->Call("sessions.history", {{"sessionKey", session_key}});
  ASSERT_TRUE(history.is_array());

  // Should have at least user + assistant messages
  EXPECT_GE(history.size(), 2u);

  // First message should be user
  bool found_user = false;
  bool found_assistant = false;
  for (const auto& msg : history) {
    std::string role = msg.value("role", "");
    if (role == "user")
      found_user = true;
    if (role == "assistant")
      found_assistant = true;
  }
  EXPECT_TRUE(found_user);
  EXPECT_TRUE(found_assistant);

  client->Disconnect();
}

TEST_F(E2ETest, E2E_ChannelsList) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  auto result = client->Call("channels.list");
  ASSERT_TRUE(result.is_array());
  EXPECT_GE(result.size(), 1u);

  // Should include CLI channel (field may be "name" or "id")
  bool has_cli = false;
  for (const auto& ch : result) {
    std::string id = ch.value("id", ch.value("name", ""));
    if (id == "cli") {
      has_cli = true;
      break;
    }
  }
  EXPECT_TRUE(has_cli);

  client->Disconnect();
}

TEST_F(E2ETest, E2E_ChainExecute) {
  auto client = make_client();
  ASSERT_TRUE(client->Connect(5000));

  std::string test_file = (workspace_dir_ / "hello.txt").string();

  auto result = client->Call(
      "chain.execute",
      {{"name", "test-chain"},
       {"steps", {{{"tool", "read"}, {"arguments", {{"path", test_file}}}}}}},
      10000);

  EXPECT_TRUE(result.contains("success"));
  EXPECT_TRUE(result["success"].get<bool>());
  EXPECT_TRUE(result.contains("final_result"));
  // The read_file tool should return the file contents
  std::string final_result = result.value("final_result", "");
  EXPECT_NE(final_result.find("hello world"), std::string::npos);

  client->Disconnect();
}

// --- Auth tests: use a separate fixture with token auth ---

class E2EAuthTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_e2e_auth_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("e2e-auth", null_sink);

    config_.agent.model = "e2e-mock-model";
    config_.gateway.port = port_;
    config_.gateway.auth.mode = "token";
    config_.gateway.auth.token = "e2e-secret-token";

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_llm_ = std::make_shared<E2EMockLLMProvider>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
    server_->SetAuth("token", "e2e-secret-token");

    quantclaw::gateway::register_rpc_handlers(*server_, session_manager_,
                                              agent_loop_, prompt_builder_,
                                              tool_registry_, config_, logger_);

    server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
      server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
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
  std::shared_ptr<E2EMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
};

TEST_F(E2EAuthTest, E2E_AuthRejectNoHello) {
  // Connect with no token; the client's automatic hello will send empty token
  // which should be rejected when auth mode=token
  std::string url = "ws://127.0.0.1:" + std::to_string(port_);
  quantclaw::gateway::GatewayClient client(url, "", logger_);

  bool connected = client.Connect(3000);
  if (connected) {
    // Connection may succeed at WebSocket level, but RPC should fail
    EXPECT_THROW(client.Call("gateway.health", {}, 3000), std::runtime_error);
  }
  // If connect() returned false, that's also acceptable (hello was rejected)

  client.Disconnect();
}

TEST_F(E2EAuthTest, E2E_AuthRejectBadToken) {
  std::string url = "ws://127.0.0.1:" + std::to_string(port_);
  quantclaw::gateway::GatewayClient client(url, "wrong-token", logger_);

  bool connected = client.Connect(3000);
  if (connected) {
    EXPECT_THROW(client.Call("gateway.health", {}, 3000), std::runtime_error);
  }

  client.Disconnect();
}

TEST_F(E2EAuthTest, E2E_AuthSuccessCorrectToken) {
  std::string url = "ws://127.0.0.1:" + std::to_string(port_);
  quantclaw::gateway::GatewayClient client(url, "e2e-secret-token", logger_);

  ASSERT_TRUE(client.Connect(5000));

  auto result = client.Call("gateway.health");
  EXPECT_EQ(result["status"], "ok");

  client.Disconnect();
}
