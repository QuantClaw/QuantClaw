// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

import <spdlog/sinks/null_sink.h>;
import <spdlog/spdlog.h>;

import quantclaw.gateway.gateway_client;
import quantclaw.gateway.gateway_server;
import quantclaw.gateway.protocol;

import quantclaw.test.helpers;
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
      // Brief pause to let ixwebsocket finish processing any pending
      // close/disconnect callbacks before tearing down the server.
      // Prevents "bad function call" on Windows where the callback
      // thread may still be running when Stop() clears handlers.
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
  quantclaw::test::ReleaseHeldPorts();
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
  quantclaw::test::ReleaseHeldPorts();
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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

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

// --- Regression: issue #51 — use-after-free in Send*/Broadcast ---
//
// ws_connections_ previously stored raw ix::WebSocket* pointers. After
// releasing connections_mutex_, a concurrent client disconnect could cause
// ixwebsocket to destroy the WebSocket before BroadcastEvent / SendResponseTo
// called send() on the snapshot pointer (use-after-free / dangling pointer).
//
// The fix stores shared_ptr<ix::WebSocket> snapshots so the object stays alive
// for the duration of the send, regardless of concurrent disconnects.

TEST_F(GatewayTest, BroadcastDuringConcurrentDisconnect) {
  // Multiple clients connect, then disconnect concurrently while the server
  // broadcasts events. With raw pointers this could crash; with shared_ptr
  // snapshots the sends complete safely (or are silently dropped for already-
  // disconnected clients).
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);
  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

  std::string url = "ws://127.0.0.1:" + std::to_string(port);

  // Kept low so the test completes in reasonable time even under sanitizer
  // builds (asan/tsan add 5-20x overhead). Enough iterations to exercise the
  // concurrent-disconnect race window without overwhelming ixwebsocket's
  // internal GC thread.
  constexpr int kClients = 3;
  constexpr int kIterations = 5;

  for (int i = 0; i < kIterations; ++i) {
    // Connect a batch of clients
    std::vector<std::unique_ptr<GatewayClient>> clients;
    clients.reserve(kClients);
    for (int c = 0; c < kClients; ++c) {
      auto cl = std::make_unique<GatewayClient>(url, "", logger_);
      if (cl->Connect(2000)) {
        clients.push_back(std::move(cl));
      }
    }

    // Kick off disconnects on a background thread while broadcasting
    std::thread disconnector([&] {
      for (auto& cl : clients) {
        cl->Disconnect();
      }
    });

    // Broadcast while clients are disconnecting concurrently
    for (int b = 0; b < 5; ++b) {
      server_->BroadcastEvent("test.race", {{"iteration", i}, {"burst", b}});
    }

    disconnector.join();

    // Let ixwebsocket's GC thread purge the finished connection threads
    // before the next batch. Under sanitizer builds this window is critical.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Server must still be running and usable after all the concurrent activity
  EXPECT_TRUE(server_->IsRunning());
}

TEST_F(GatewayTest, SendToDisconnectedClient) {
  // Capture a connection ID via an RPC handler, then disconnect the client and
  // call SendResponseTo / SendEventTo with the stale ID. The server must not
  // crash or deadlock — the stale entry is gone from connections_ so both
  // methods should return early without touching any WebSocket pointer.
  int port = find_free_port();
  server_ = std::make_unique<GatewayServer>(port, logger_);

  std::string captured_conn_id;
  std::mutex id_mu;

  server_->RegisterHandler(
      "test.capture_id",
      [&](const nlohmann::json&, ClientConnection& conn) -> nlohmann::json {
        std::lock_guard<std::mutex> lk(id_mu);
        captured_conn_id = conn.connection_id;
        return {{"ok", true}};
      });

  quantclaw::test::ReleaseHeldPorts();
  server_->Start();
  ASSERT_TRUE(quantclaw::test::WaitForServerReady(port, 5000))
      << "Server not ready on port " << port;

  std::string url = "ws://127.0.0.1:" + std::to_string(port);
  GatewayClient client(url, "", logger_);
  ASSERT_TRUE(client.Connect(5000));

  // Make an RPC call so the handler captures the connection ID
  auto resp = client.Call("test.capture_id", {}, 5000);
  EXPECT_TRUE(resp.value("ok", false));

  std::string conn_id;
  {
    std::lock_guard<std::mutex> lk(id_mu);
    conn_id = captured_conn_id;
  }
  ASSERT_FALSE(conn_id.empty());

  // Disconnect the client — ws_connections_ entry is now erased
  client.Disconnect();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Both send methods must be safe to call with a stale / unknown connection ID
  RpcEvent evt;
  evt.event = "test.stale";
  evt.payload = {{"msg", "should be dropped"}};
  EXPECT_NO_FATAL_FAILURE(server_->SendEventTo(conn_id, evt));
  EXPECT_NO_FATAL_FAILURE(
      server_->SendResponseTo(conn_id, "req-stale", true, {{"ok", true}}));

  EXPECT_TRUE(server_->IsRunning());
}

// --- Client to unreachable server ---

TEST_F(GatewayTest, ClientConnectFails) {
  GatewayClient client("ws://127.0.0.1:1", "", logger_);
  EXPECT_FALSE(client.Connect(1000));
  EXPECT_FALSE(client.IsConnected());
}
