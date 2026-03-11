// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

using namespace quantclaw::gateway;

class GatewayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
      server_.reset();
    }
  }

  int find_free_port() {
    return quantclaw::test::FindFreePort();
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<GatewayServer> server_;
};

// --- Server lifecycle ---

TEST_F(GatewayTest, ServerStartStop) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  EXPECT_FALSE(server_->IsRunning());
  server_->Start();
  EXPECT_TRUE(server_->IsRunning());
  EXPECT_EQ(server_->GetPort(), port);
  EXPECT_EQ(server_->GetConnectionCount(), 0u);

  server_->Stop();
  EXPECT_FALSE(server_->IsRunning());
}

TEST_F(GatewayTest, UptimeIncreases) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->Start();

  EXPECT_GE(server_->GetUptimeSeconds(), 0);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_GE(server_->GetUptimeSeconds(), 1);

  server_->Stop();
}

// --- RPC handler registration ---

TEST_F(GatewayTest, RegisterHandler) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  bool called = false;
  server_->RegisterHandler(
      "test.method",
      [&called](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        called = true;
        return {{"result", "ok"}};
      });

  // Handler registered but not called yet
  EXPECT_FALSE(called);
}

// --- Client connection + RPC ---

TEST_F(GatewayTest, ClientConnectAndCall) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  server_->RegisterHandler(
      "test.echo",
      [](const nlohmann::json& params, ClientConnection&) -> nlohmann::json {
        return {{"echo", params.value("msg", "")}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Create client
  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);

  ASSERT_TRUE(client.Connect(5000));
  EXPECT_TRUE(client.IsConnected());

  // RPC call
  auto result = client.Call("test.echo", {{"msg", "hello"}});
  EXPECT_EQ(result["echo"], "hello");

  client.Disconnect();
  EXPECT_FALSE(client.IsConnected());
}

TEST_F(GatewayTest, HealthRpc) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  server_->RegisterHandler(
      methods::kGatewayHealth,
      [](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        return {{"status", "ok"}, {"version", "0.2.0"}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  auto result = client.Call("gateway.health");
  EXPECT_EQ(result["status"], "ok");
  EXPECT_EQ(result["version"], "0.2.0");

  client.Disconnect();
}

TEST_F(GatewayTest, UnknownMethodReturnsError) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  EXPECT_THROW(client.Call("nonexistent.method"), std::runtime_error);

  client.Disconnect();
}

// --- Multiple clients ---

TEST_F(GatewayTest, MultipleClients) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  server_->RegisterHandler(
      "test.ping",
      [](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        return {{"pong", true}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string url = "ws://127.0.0.1:" + std::to_string(port);

  GatewayClient c1(url, "", logger_);
  GatewayClient c2(url, "", logger_);

  ASSERT_TRUE(c1.Connect(5000));
  ASSERT_TRUE(c2.Connect(5000));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_GE(server_->GetConnectionCount(), 2u);

  auto r1 = c1.Call("test.ping");
  auto r2 = c2.Call("test.ping");

  EXPECT_TRUE(r1["pong"]);
  EXPECT_TRUE(r2["pong"]);

  c1.Disconnect();
  c2.Disconnect();
}

// --- Broadcast events ---

TEST_F(GatewayTest, BroadcastEvent) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  // Subscribe to events
  std::atomic<bool> received{false};
  std::mutex rx_mu;
  std::string received_data;
  client.Subscribe("test.event",
                   [&](const std::string&, const nlohmann::json& payload) {
                     std::lock_guard<std::mutex> lk(rx_mu);
                     received_data = payload.value("msg", "");
                     received.store(true);
                   });

  // Broadcast
  server_->BroadcastEvent("test.event", {{"msg", "broadcast!"}});

  // Wait for delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_TRUE(received.load());
  {
    std::lock_guard<std::mutex> lk(rx_mu);
    EXPECT_EQ(received_data, "broadcast!");
  }

  client.Disconnect();
}

// --- Auth enforcement ---

TEST_F(GatewayTest, AuthModeNoneAllowsAll) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->SetAuth("none", "");

  server_->RegisterHandler(
      "test.ping",
      [](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        return {{"pong", true}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  auto result = client.Call("test.ping");
  EXPECT_TRUE(result["pong"]);

  client.Disconnect();
}

TEST_F(GatewayTest, AuthTokenValidationSuccess) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->SetAuth("token", "secret123");

  server_->RegisterHandler(
      "test.ping",
      [](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        return {{"pong", true}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Client with correct token
  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "secret123", logger_);
  ASSERT_TRUE(client.Connect(5000));

  auto result = client.Call("test.ping");
  EXPECT_TRUE(result["pong"]);

  client.Disconnect();
}

TEST_F(GatewayTest, AuthTokenValidationFailure) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->SetAuth("token", "correct-token");

  server_->RegisterHandler(
      "test.ping",
      [](const nlohmann::json&, ClientConnection&) -> nlohmann::json {
        return {{"pong", true}};
      });

  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Client with wrong token — hello should fail, subsequent RPC should fail
  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "wrong-token", logger_);

  // connect() attempts hello with the token; server should reject
  // The client may or may not report connected depending on implementation
  // but RPC calls should fail
  bool connected = client.Connect(3000);
  if (connected) {
    // Even if WebSocket connected, RPC should fail (not authenticated)
    EXPECT_THROW(client.Call("test.ping", {}, 3000), std::runtime_error);
  }

  client.Disconnect();
}

TEST_F(GatewayTest, SetAuthMode) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  server_->SetAuth("none", "");
  EXPECT_EQ(server_->GetAuthMode(), "none");

  server_->SetAuth("token", "secret");
  EXPECT_EQ(server_->GetAuthMode(), "token");
}

// --- State snapshot ---

TEST_F(GatewayTest, BuildSnapshotContainsExpectedFields) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->SetAuth("none", "");
  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto snapshot = server_->BuildSnapshot();

  EXPECT_TRUE(snapshot.contains("presence"));
  EXPECT_TRUE(snapshot["presence"].is_array());
  EXPECT_TRUE(snapshot.contains("health"));
  EXPECT_TRUE(snapshot.contains("stateVersion"));
  EXPECT_TRUE(snapshot.contains("uptimeMs"));
  EXPECT_TRUE(snapshot.contains("authMode"));
  EXPECT_EQ(snapshot["authMode"], "none");
  EXPECT_TRUE(snapshot.contains("sessionDefaults"));
  EXPECT_EQ(snapshot["sessionDefaults"]["defaultAgentId"], "main");
  EXPECT_EQ(snapshot["sessionDefaults"]["mainSessionKey"], "agent:main:main");
}

TEST_F(GatewayTest, HelloResponseContainsSnapshot) {
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  server_->SetAuth("none", "");
  server_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Connect a client — the hello-ok response should contain a snapshot
  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  // After connecting, the snapshot was part of the hello-ok payload.
  // We can verify by checking the server's snapshot directly.
  auto snapshot = server_->BuildSnapshot();
  EXPECT_TRUE(snapshot.contains("presence"));
  EXPECT_GE(snapshot["presence"].size(), 1u);  // at least our client

  client.Disconnect();
}

// --- Client to unreachable server ---

TEST_F(GatewayTest, ClientConnectFails) {
  GatewayClient client("ws://127.0.0.1:1", "", logger_);
  EXPECT_FALSE(client.Connect(1000));
  EXPECT_FALSE(client.IsConnected());
}
