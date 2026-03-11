// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include <httplib.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/providers/llm_provider.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/web/api_routes.hpp"
#include "quantclaw/web/web_server.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Mock LLM provider
class ApiMockLLMProvider : public quantclaw::LLMProvider {
 public:
  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest&) override {
    quantclaw::ChatCompletionResponse resp;
    resp.content = "mock api reply";
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest&,
      std::function<void(const quantclaw::ChatCompletionResponse&)> cb)
      override {
    quantclaw::ChatCompletionResponse delta;
    delta.content = "mock api reply";
    cb(delta);

    quantclaw::ChatCompletionResponse end;
    end.content = "mock api reply";
    end.is_stream_end = true;
    cb(end);
  }

  std::string GetProviderName() const override {
    return "api-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock"};
  }
};

class ApiRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_api_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("api_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 512;
    config_.gateway.port = gw_port_;
    config_.gateway.auth.mode = "none";

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_llm_ = std::make_shared<ApiMockLLMProvider>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    // Gateway server (needed for uptime/connections in /api/health and
    // /api/status)
    gateway_server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(gw_port_, logger_);
    gateway_server_->SetAuth("none", "");
    gateway_server_->Start();

    // HTTP API server
    http_server_ =
        std::make_unique<quantclaw::web::WebServer>(http_port_, logger_);
    http_server_->EnableCors("*");

    quantclaw::web::register_api_routes(
        *http_server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, *gateway_server_, logger_);

    http_server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  void TearDown() override {
    if (http_server_) {
      http_server_->Stop();
      http_server_.reset();
    }
    if (gateway_server_) {
      gateway_server_->Stop();
      gateway_server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  httplib::Client make_client() {
    return httplib::Client("127.0.0.1", http_port_);
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
  }

  int gw_port_ = next_port();
  int http_port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  quantclaw::QuantClawConfig config_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<ApiMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> gateway_server_;
  std::unique_ptr<quantclaw::web::WebServer> http_server_;
};

// --- Health ---

TEST_F(ApiRoutesTest, HealthEndpoint) {
  auto cli = make_client();
  auto res = cli.Get("/api/health");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["status"], "ok");
  EXPECT_TRUE(body.contains("uptime"));
  EXPECT_EQ(body["version"], "0.2.0");
}

// --- Status ---

TEST_F(ApiRoutesTest, StatusEndpoint) {
  auto cli = make_client();
  auto res = cli.Get("/api/status");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["running"], true);
  EXPECT_EQ(body["port"], gw_port_);
  EXPECT_TRUE(body.contains("uptime"));
  EXPECT_TRUE(body.contains("sessions"));
}

// --- Config ---

TEST_F(ApiRoutesTest, ConfigGet) {
  auto cli = make_client();
  auto res = cli.Get("/api/config");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.contains("agent"));
  EXPECT_TRUE(body.contains("gateway"));
  EXPECT_EQ(body["agent"]["model"], "mock-model");
}

TEST_F(ApiRoutesTest, ConfigGetWithPath) {
  auto cli = make_client();
  auto res = cli.Get("/api/config?path=gateway.port");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body.get<int>(), gw_port_);
}

TEST_F(ApiRoutesTest, ConfigGetBadPath) {
  auto cli = make_client();
  auto res = cli.Get("/api/config?path=nonexistent.key");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.contains("error"));
}

// --- Sessions ---

TEST_F(ApiRoutesTest, SessionsListEmpty) {
  auto cli = make_client();
  auto res = cli.Get("/api/sessions");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.is_array());
  EXPECT_EQ(body.size(), 0u);
}

// --- Agent ---

TEST_F(ApiRoutesTest, AgentRequest) {
  auto cli = make_client();
  cli.set_read_timeout(10);
  auto res = cli.Post("/api/agent/request",
                      R"({"message":"Hello","sessionKey":"api:test:main"})",
                      "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["sessionKey"], "api:test:main");
  EXPECT_FALSE(body["response"].get<std::string>().empty());
}

TEST_F(ApiRoutesTest, AgentRequestEmptyMessage) {
  auto cli = make_client();
  auto res =
      cli.Post("/api/agent/request", R"({"message":""})", "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ApiRoutesTest, AgentStop) {
  auto cli = make_client();
  auto res = cli.Post("/api/agent/stop", "", "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["ok"], true);
}

// --- Sessions history ---

TEST_F(ApiRoutesTest, SessionsHistory) {
  // Create a session with a message first
  session_manager_->GetOrCreate("api:hist:test");
  session_manager_->AppendMessage("api:hist:test", "user", "hello");

  auto cli = make_client();
  auto res = cli.Get("/api/sessions/history?sessionKey=api:hist:test");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  ASSERT_TRUE(body.is_array());
  ASSERT_EQ(body.size(), 1u);
  EXPECT_EQ(body[0]["role"], "user");
}

TEST_F(ApiRoutesTest, SessionsHistoryMissingKey) {
  auto cli = make_client();
  auto res = cli.Get("/api/sessions/history");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

// --- Sessions delete ---

TEST_F(ApiRoutesTest, SessionsDelete) {
  session_manager_->GetOrCreate("api:del:test");

  auto cli = make_client();
  auto res = cli.Post("/api/sessions/delete",
                      R"({"sessionKey":"api:del:test"})", "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto sessions = session_manager_->ListSessions();
  EXPECT_TRUE(sessions.empty());
}

// --- Channels ---

TEST_F(ApiRoutesTest, ChannelsList) {
  auto cli = make_client();
  auto res = cli.Get("/api/channels");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  ASSERT_TRUE(body.is_array());
  EXPECT_GE(body.size(), 1u);
  EXPECT_EQ(body[0]["name"], "cli");
}

// --- CORS ---

TEST_F(ApiRoutesTest, CorsHeaders) {
  auto cli = make_client();
  auto res = cli.Get("/api/health");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->get_header_value("Access-Control-Allow-Origin"), "*");
  EXPECT_FALSE(res->get_header_value("Access-Control-Allow-Methods").empty());
}

// --- Auth ---

class ApiRoutesAuthTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_api_auth_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("api_auth_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.gateway.port = gw_port_;

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    mock_llm_ = std::make_shared<ApiMockLLMProvider>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    gateway_server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(gw_port_, logger_);
    gateway_server_->SetAuth("none", "");
    gateway_server_->Start();

    http_server_ =
        std::make_unique<quantclaw::web::WebServer>(http_port_, logger_);
    http_server_->SetAuthToken("secret-token-123");

    quantclaw::web::register_api_routes(
        *http_server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, *gateway_server_, logger_);

    http_server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  void TearDown() override {
    if (http_server_) {
      http_server_->Stop();
      http_server_.reset();
    }
    if (gateway_server_) {
      gateway_server_->Stop();
      gateway_server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
  }

  int gw_port_ = next_port();
  int http_port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  quantclaw::QuantClawConfig config_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<ApiMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> gateway_server_;
  std::unique_ptr<quantclaw::web::WebServer> http_server_;
};

TEST_F(ApiRoutesAuthTest, RejectWithoutToken) {
  httplib::Client cli("127.0.0.1", http_port_);
  auto res = cli.Get("/api/status");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 401);
}

TEST_F(ApiRoutesAuthTest, AllowWithCorrectToken) {
  httplib::Client cli("127.0.0.1", http_port_);
  httplib::Headers headers = {{"Authorization", "Bearer secret-token-123"}};
  auto res = cli.Get("/api/status", headers);

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
}

TEST_F(ApiRoutesAuthTest, HealthBypassesAuth) {
  httplib::Client cli("127.0.0.1", http_port_);
  auto res = cli.Get("/api/health");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
}

// --- Config reload endpoint ---

class ApiRoutesReloadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_api_reload_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("api_reload_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 512;
    config_.gateway.port = gw_port_;
    config_.gateway.auth.mode = "none";

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_llm_ = std::make_shared<ApiMockLLMProvider>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    reload_called_ = false;
    reload_fn_ = [this]() { reload_called_ = true; };

    gateway_server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(gw_port_, logger_);
    gateway_server_->SetAuth("none", "");
    gateway_server_->Start();

    http_server_ =
        std::make_unique<quantclaw::web::WebServer>(http_port_, logger_);
    http_server_->EnableCors("*");

    quantclaw::web::register_api_routes(
        *http_server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, *gateway_server_, logger_, reload_fn_);

    http_server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  void TearDown() override {
    if (http_server_) {
      http_server_->Stop();
      http_server_.reset();
    }
    if (gateway_server_) {
      gateway_server_->Stop();
      gateway_server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  httplib::Client make_client() {
    return httplib::Client("127.0.0.1", http_port_);
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
  }

  int gw_port_ = next_port();
  int http_port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  quantclaw::QuantClawConfig config_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<ApiMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> gateway_server_;
  std::unique_ptr<quantclaw::web::WebServer> http_server_;
  std::function<void()> reload_fn_;
  bool reload_called_;
};

TEST_F(ApiRoutesReloadTest, ConfigReloadEndpoint) {
  auto cli = make_client();
  auto res = cli.Post("/api/config/reload", "", "application/json");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["ok"], true);
  EXPECT_TRUE(reload_called_);
}

// ================================================================
// Gateway info endpoint test (for dashboard UI discovery)
// ================================================================

class ApiGatewayInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_api_gwinfo_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("api_gwinfo_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 3;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 512;
    config_.gateway.port = gw_port_;
    config_.gateway.auth.mode = "none";

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_llm_ = std::make_shared<ApiMockLLMProvider>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    gateway_server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(gw_port_, logger_);
    gateway_server_->SetAuth("none", "");
    gateway_server_->Start();

    http_server_ =
        std::make_unique<quantclaw::web::WebServer>(http_port_, logger_);
    http_server_->EnableCors("*");

    quantclaw::web::register_api_routes(
        *http_server_, session_manager_, agent_loop_, prompt_builder_,
        tool_registry_, config_, *gateway_server_, logger_);

    // Add gateway-info endpoint (same as in gateway_commands.cpp)
    int ws_port = gw_port_;
    http_server_->AddRawRoute(
        "/api/gateway-info", "GET",
        [ws_port](const httplib::Request&, httplib::Response& res) {
          nlohmann::json info = {
              {"wsUrl", "ws://localhost:" + std::to_string(ws_port)},
              {"wsPort", ws_port},
              {"version", "0.2.0"}};
          res.status = 200;
          res.set_content(info.dump(), "application/json");
        });

    http_server_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  void TearDown() override {
    if (http_server_) {
      http_server_->Stop();
      http_server_.reset();
    }
    if (gateway_server_) {
      gateway_server_->Stop();
      gateway_server_.reset();
    }
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  httplib::Client make_client() {
    return httplib::Client("127.0.0.1", http_port_);
  }

  static int next_port() {
    return quantclaw::test::FindFreePort();
  }

  int gw_port_ = next_port();
  int http_port_ = next_port();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  quantclaw::QuantClawConfig config_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<ApiMockLLMProvider> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> gateway_server_;
  std::unique_ptr<quantclaw::web::WebServer> http_server_;
};

TEST_F(ApiGatewayInfoTest, GatewayInfoEndpoint) {
  auto cli = make_client();
  auto res = cli.Get("/api/gateway-info");

  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.contains("wsPort"));
  EXPECT_EQ(body["wsPort"].get<int>(), gw_port_);
  EXPECT_TRUE(body.contains("wsUrl"));
  EXPECT_TRUE(body.contains("version"));
  EXPECT_EQ(body["version"], "0.2.0");
}
