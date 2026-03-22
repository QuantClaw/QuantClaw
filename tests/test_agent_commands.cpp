// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Integration tests for AgentCommands: spins up an in-process mock gateway
// and exercises the full RequestCommand / StopCommand flow end-to-end.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/cli/agent_commands.hpp"
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
#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// Forward declare register_rpc_handlers
namespace quantclaw {
class ProviderRegistry;
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

// --- Capture helpers ---
// Use C++ stream redirection instead of fd-level dup2 to avoid
// TSAN data races with background WebSocket threads.

static std::string capture_stdout(std::function<void()> fn) {
  std::ostringstream oss;
  std::streambuf* saved = std::cout.rdbuf(oss.rdbuf());
  try {
    fn();
  } catch (...) {
    std::cout.rdbuf(saved);
    throw;
  }
  std::cout.flush();
  std::cout.rdbuf(saved);
  return oss.str();
}

static std::string capture_stderr(std::function<void()> fn) {
  std::ostringstream oss;
  std::streambuf* saved = std::cerr.rdbuf(oss.rdbuf());
  try {
    fn();
  } catch (...) {
    std::cerr.rdbuf(saved);
    throw;
  }
  std::cerr.flush();
  std::cerr.rdbuf(saved);
  return oss.str();
}

// --- Mock LLM Provider ---

class AgentCmdMockLLM : public quantclaw::LLMProvider {
 public:
  std::string response_text = "Mock agent response.";
  bool stream_should_fail = false;
  std::string stream_error_message = "Mock streaming failure.";

  quantclaw::ChatCompletionResponse
  ChatCompletion(const quantclaw::ChatCompletionRequest& /*req*/) override {
    quantclaw::ChatCompletionResponse resp;
    resp.content = response_text;
    resp.finish_reason = "stop";
    return resp;
  }

  void ChatCompletionStream(
      const quantclaw::ChatCompletionRequest& /*req*/,
      std::function<void(const quantclaw::ChatCompletionResponse&)> cb)
      override {
    if (stream_should_fail) {
      throw std::runtime_error(stream_error_message);
    }

    quantclaw::ChatCompletionResponse delta;
    delta.content = response_text;
    delta.is_stream_end = false;
    cb(delta);

    quantclaw::ChatCompletionResponse end;
    end.content = response_text;
    end.is_stream_end = true;
    end.finish_reason = "stop";
    cb(end);
  }

  std::string GetProviderName() const override {
    return "agent-cmd-mock";
  }
  std::vector<std::string> GetSupportedModels() const override {
    return {"mock-model"};
  }
};

// --- Test fixture ---

class AgentCommandsIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("agent_cmd_test");
    workspace_dir_ = test_dir_ / "workspace";
    sessions_dir_ = test_dir_ / "sessions";
    std::filesystem::create_directories(workspace_dir_);
    std::filesystem::create_directories(sessions_dir_);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("agent_cmd_test", null_sink);

    config_.agent.model = "mock-model";
    config_.agent.max_iterations = 5;
    config_.agent.temperature = 0.0;
    config_.agent.max_tokens = 1024;
    config_.gateway.port = port_;
    config_.gateway.auth.mode = "none";
    config_.gateway.auth.token = "";

    memory_manager_ =
        std::make_shared<quantclaw::MemoryManager>(workspace_dir_, logger_);
    skill_loader_ = std::make_shared<quantclaw::SkillLoader>(logger_);
    tool_registry_ = std::make_shared<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();

    mock_llm_ = std::make_shared<AgentCmdMockLLM>();
    agent_loop_ = std::make_shared<quantclaw::AgentLoop>(
        memory_manager_, skill_loader_, tool_registry_, mock_llm_,
        config_.agent, logger_);
    session_manager_ =
        std::make_shared<quantclaw::SessionManager>(sessions_dir_, logger_);
    prompt_builder_ = std::make_shared<quantclaw::PromptBuilder>(
        memory_manager_, skill_loader_, tool_registry_);

    server_ =
        std::make_unique<quantclaw::gateway::GatewayServer>(port_, logger_);
    server_->SetAuth(config_.gateway.auth.mode, config_.gateway.auth.token);
    quantclaw::gateway::register_rpc_handlers(*server_, session_manager_,
                                              agent_loop_, prompt_builder_,
                                              tool_registry_, config_, logger_);
    quantclaw::test::ReleaseHeldPorts();
    server_->Start();

    // Wait until the server actually accepts connections instead of a
    // blind sleep.  Under heavy CI load or TSan slowdown the previous
    // 200 ms was sometimes not enough, causing flaky timeouts.
    ASSERT_TRUE(quantclaw::test::WaitForServerReady(port_, 5000))
        << "Server not ready on port " << port_;

    // Prepare AgentCommands pointing at our mock gateway.
    // Use a 30 s timeout instead of the production default (120 s) so that
    // a stuck test fails fast rather than exhausting the ctest timeout.
    agent_cmds_ = std::make_unique<quantclaw::cli::AgentCommands>(logger_);
    agent_cmds_->SetGatewayUrl("ws://127.0.0.1:" + std::to_string(port_));
    agent_cmds_->SetDefaultTimeoutMs(30000);
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

  int port_ = quantclaw::test::FindFreePort();
  std::filesystem::path test_dir_;
  std::filesystem::path workspace_dir_;
  std::filesystem::path sessions_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  quantclaw::QuantClawConfig config_;
  std::shared_ptr<quantclaw::MemoryManager> memory_manager_;
  std::shared_ptr<quantclaw::SkillLoader> skill_loader_;
  std::shared_ptr<quantclaw::ToolRegistry> tool_registry_;
  std::shared_ptr<AgentCmdMockLLM> mock_llm_;
  std::shared_ptr<quantclaw::AgentLoop> agent_loop_;
  std::shared_ptr<quantclaw::SessionManager> session_manager_;
  std::shared_ptr<quantclaw::PromptBuilder> prompt_builder_;
  std::unique_ptr<quantclaw::gateway::GatewayServer> server_;
  std::unique_ptr<quantclaw::cli::AgentCommands> agent_cmds_;
};

// ========== Scenario 1: basic -m flag ==========

TEST_F(AgentCommandsIntegrationTest, RequestWithDashM) {
  int ret = -1;
  auto out = capture_stdout(
      [&]() { ret = agent_cmds_->RequestCommand({"-m", "hello"}); });
  EXPECT_EQ(ret, 0);
  // Streaming output should contain the mock response
  EXPECT_NE(out.find("Mock agent response"), std::string::npos);
}

// ========== Scenario 2: --message long form ==========

TEST_F(AgentCommandsIntegrationTest, RequestWithLongMessageFlag) {
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand({"--message", "test long form"});
  });
  EXPECT_EQ(ret, 0);
  EXPECT_NE(out.find("Mock agent response"), std::string::npos);
}

// ========== Scenario 3: positional arg as message ==========

TEST_F(AgentCommandsIntegrationTest, RequestWithPositionalArg) {
  int ret = -1;
  auto out = capture_stdout(
      [&]() { ret = agent_cmds_->RequestCommand({"hello positional"}); });
  EXPECT_EQ(ret, 0);
  EXPECT_NE(out.find("Mock agent response"), std::string::npos);
}

// ========== Scenario 4: --json output ==========

TEST_F(AgentCommandsIntegrationTest, RequestJsonOutput) {
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand({"-m", "json test", "--json"});
  });

  EXPECT_EQ(ret, 0);
  // JSON output should be valid and contain response/sessionKey
  auto json = nlohmann::json::parse(out, nullptr, false);
  EXPECT_FALSE(json.is_discarded()) << "Output is not valid JSON: " << out;
  EXPECT_TRUE(json.contains("response") || json.contains("sessionKey"))
      << "JSON missing expected fields: " << out;
}

// ========== Scenario 5: custom session key ==========

TEST_F(AgentCommandsIntegrationTest, RequestCustomSession) {
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand(
        {"-m", "session test", "--session-id", "custom:session:key", "--json"});
  });
  EXPECT_EQ(ret, 0);
  auto json = nlohmann::json::parse(out, nullptr, false);
  EXPECT_FALSE(json.is_discarded());
  if (json.contains("sessionKey")) {
    EXPECT_EQ(json["sessionKey"].get<std::string>(), "custom:session:key");
  }
}

// ========== Scenario 6: -s short session flag ==========

TEST_F(AgentCommandsIntegrationTest, RequestShortSessionFlag) {
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand(
        {"-m", "session test", "-s", "short:sess", "--json"});
  });
  EXPECT_EQ(ret, 0);
  auto json = nlohmann::json::parse(out, nullptr, false);
  EXPECT_FALSE(json.is_discarded());
  if (json.contains("sessionKey")) {
    EXPECT_EQ(json["sessionKey"].get<std::string>(), "short:sess");
  }
}

// ========== Scenario 7: --no-session generates ephemeral key ==========

TEST_F(AgentCommandsIntegrationTest, RequestNoSessionGeneratesEphemeral) {
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand(
        {"-m", "ephemeral test", "--no-session", "--json"});
  });
  EXPECT_EQ(ret, 0);
  auto json = nlohmann::json::parse(out, nullptr, false);
  EXPECT_FALSE(json.is_discarded());
  if (json.contains("sessionKey")) {
    auto key = json["sessionKey"].get<std::string>();
    // Ephemeral keys start with "ephemeral:"
    EXPECT_EQ(key.substr(0, 10), "ephemeral:");
  }
}

// ========== Scenario 8: --no-session keys are unique ==========

TEST_F(AgentCommandsIntegrationTest, NoSessionKeysAreUnique) {
  std::string key1, key2;

  auto run = [&](std::string& out_key) {
    auto out = capture_stdout([&]() {
      agent_cmds_->RequestCommand({"-m", "test", "--no-session", "--json"});
    });
    auto json = nlohmann::json::parse(out, nullptr, false);
    if (!json.is_discarded() && json.contains("sessionKey")) {
      out_key = json["sessionKey"].get<std::string>();
    }
  };

  run(key1);
  // Brief sleep to ensure different timestamp
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  run(key2);

  EXPECT_FALSE(key1.empty());
  EXPECT_FALSE(key2.empty());
  EXPECT_NE(key1, key2) << "Two --no-session calls must produce different keys";
}

// ========== Scenario 9: session persistence (same key → shared history)
// ==========

TEST_F(AgentCommandsIntegrationTest, SessionPersistence) {
  // First request creates session
  int ret1 = -1;
  capture_stdout([&]() {
    ret1 = agent_cmds_->RequestCommand(
        {"-m", "first message", "--session-id", "persist:test"});
  });
  EXPECT_EQ(ret1, 0);

  // Second request with same session key should succeed
  int ret2 = -1;
  capture_stdout([&]() {
    ret2 = agent_cmds_->RequestCommand(
        {"-m", "second message", "-s", "persist:test"});
  });
  EXPECT_EQ(ret2, 0);

  // Verify session was persisted — check sessions directory for the key
  bool found_session = false;
  for (auto& p : std::filesystem::recursive_directory_iterator(sessions_dir_)) {
    if (p.is_regular_file()) {
      found_session = true;
      break;
    }
  }
  EXPECT_TRUE(found_session) << "Session should be persisted to disk";
}

// ========== Scenario 10: stop command ==========

TEST_F(AgentCommandsIntegrationTest, StopCommand) {
  int ret = -1;
  auto out = capture_stdout([&]() { ret = agent_cmds_->StopCommand({}); });
  EXPECT_EQ(ret, 0);
  EXPECT_NE(out.find("Agent stopped"), std::string::npos);
}

// ========== Scenario 11: --timeout flag is parsed ==========

TEST_F(AgentCommandsIntegrationTest, RequestWithTimeout) {
  // 5-second timeout should be enough for mock
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand({"-m", "timeout test", "--timeout", "5"});
  });
  EXPECT_EQ(ret, 0);
  EXPECT_NE(out.find("Mock agent response"), std::string::npos);
}

// ========== Scenario 12: auth token required but not provided ==========

TEST_F(AgentCommandsIntegrationTest, AuthTokenMismatchReturnsError) {
  // Reconfigure server with auth
  server_->Stop();
  server_.reset();

  int auth_port = quantclaw::test::FindFreePort();
  auto auth_config = config_;
  auth_config.gateway.port = auth_port;
  auth_config.gateway.auth.mode = "token";
  auth_config.gateway.auth.token = "secret123";

  server_ =
      std::make_unique<quantclaw::gateway::GatewayServer>(auth_port, logger_);
  server_->SetAuth(auth_config.gateway.auth.mode,
                   auth_config.gateway.auth.token);
  quantclaw::gateway::register_rpc_handlers(
      *server_, session_manager_, agent_loop_, prompt_builder_, tool_registry_,
      auth_config, logger_);
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(auth_port, 5000))
      << "Server not ready on port " << auth_port;

  // Agent without auth token
  auto no_auth_cmds = std::make_unique<quantclaw::cli::AgentCommands>(logger_);
  no_auth_cmds->SetGatewayUrl("ws://127.0.0.1:" + std::to_string(auth_port));
  // Don't set auth token

  int ret = -1;
  auto err = capture_stderr([&]() {
    ret = no_auth_cmds->RequestCommand({"-m", "should fail", "--timeout", "5"});
  });
  // Should fail — either connection refused or auth error
  EXPECT_EQ(ret, 1);
}

// ========== Scenario 13: custom mock response ==========

TEST_F(AgentCommandsIntegrationTest, CustomMockResponse) {
  mock_llm_->response_text = "Custom reply: 42";
  int ret = -1;
  auto out = capture_stdout([&]() {
    ret = agent_cmds_->RequestCommand({"-m", "what is the answer?"});
  });
  EXPECT_EQ(ret, 0);
  EXPECT_NE(out.find("Custom reply: 42"), std::string::npos);
}

TEST_F(AgentCommandsIntegrationTest, StreamingErrorsReachStderr) {
  mock_llm_->stream_should_fail = true;
  mock_llm_->stream_error_message = "Mock streaming exploded";

  int ret = -1;
  auto err = capture_stderr(
      [&]() { ret = agent_cmds_->RequestCommand({"-m", "please fail"}); });

  EXPECT_EQ(ret, 1);
  EXPECT_NE(err.find("Mock streaming exploded"), std::string::npos);
}
