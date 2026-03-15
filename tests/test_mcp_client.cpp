// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define CLOSE_SOCKET(s) closesocket(s)
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET(s) close(s)
static constexpr socket_t kInvalidSocket = -1;
#endif

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/mcp/mcp_client.hpp"

#include <gtest/gtest.h>

using namespace quantclaw::mcp;

#ifdef _WIN32
// RAII struct to initialise/cleanup Winsock for the test process
struct WinsockInit {
  WinsockInit() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~WinsockInit() {
    WSACleanup();
  }
};
static WinsockInit winsock_init_;
#endif

// Minimal HTTP server that returns a canned JSON-RPC response
class MiniHTTPServer {
 public:
  MiniHTTPServer() : running_(false), port_(0), server_fd_(kInvalidSocket) {}

  ~MiniHTTPServer() {
    stop();
  }

  int start(const std::string& canned_body) {
    canned_body_ = canned_body;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == kInvalidSocket)
      return -1;

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS picks a free port

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      CLOSE_SOCKET(server_fd_);
      server_fd_ = kInvalidSocket;
      return -1;
    }

    socklen_t len = sizeof(addr);
    getsockname(server_fd_, (struct sockaddr*)&addr, &len);
    port_ = ntohs(addr.sin_port);

    listen(server_fd_, 1);
    running_ = true;

    thread_ = std::thread([this]() {
      while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd_, &fds);
        struct timeval tv = {0, 100000};  // 100ms
        int nfds = 0;
#ifndef _WIN32
        nfds = static_cast<int>(server_fd_) + 1;
#endif
        if (select(nfds, &fds, nullptr, nullptr, &tv) > 0) {
          socket_t client_fd = accept(server_fd_, nullptr, nullptr);
          if (client_fd != kInvalidSocket) {
            handle_client(client_fd);
          }
        }
      }
    });

    return port_;
  }

  void stop() {
    running_ = false;
    if (thread_.joinable())
      thread_.join();
    if (server_fd_ != kInvalidSocket) {
      CLOSE_SOCKET(server_fd_);
      server_fd_ = kInvalidSocket;
    }
  }

  int port() const {
    return port_;
  }

 private:
  void handle_client(socket_t fd) {
    // Read the full HTTP request (we only need to drain it)
    char buf[4096];
    recv(fd, buf, sizeof(buf), 0);

    // Send HTTP response with canned body
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(canned_body_.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        canned_body_;

    send(fd, response.c_str(), static_cast<int>(response.size()), 0);
    CLOSE_SOCKET(fd);
  }

  std::atomic<bool> running_;
  int port_;
  socket_t server_fd_;
  std::string canned_body_;
  std::thread thread_;
};

class MCPClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_mcp_client", null_sink);
  }

  std::shared_ptr<spdlog::logger> logger_;
};

// --- Construction ---

TEST_F(MCPClientTest, ConstructWithUrl) {
  EXPECT_NO_THROW(MCPClient("http://localhost:9999", logger_));
}

// --- Error handling: unreachable server ---

TEST_F(MCPClientTest, ListToolsUnreachableThrows) {
  // Port 1 is almost certainly not listening
  MCPClient client("http://127.0.0.1:1", logger_);
  // May throw runtime_error (CURL fail) or parse_error (empty response)
  EXPECT_ANY_THROW(client.ListTools());
}

TEST_F(MCPClientTest, CallToolUnreachableReturnsError) {
  MCPClient client("http://127.0.0.1:1", logger_);
  auto resp = client.CallTool("test", nlohmann::json::object());
  EXPECT_FALSE(resp.error.empty());
  EXPECT_TRUE(resp.result.is_null());
}

// --- Success paths with MiniHTTPServer ---

TEST_F(MCPClientTest, ListToolsParsesSingleTool) {
  MiniHTTPServer server;
  nlohmann::json body = {{"jsonrpc", "2.0"},
                         {"id", 1},
                         {"result",
                          {{"tools",
                            {{{"name", "my_tool"},
                              {"description", "does stuff"},
                              {"parameters", {{"type", "object"}}}}}}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto tools = client.ListTools();

  ASSERT_EQ(tools.size(), 1u);
  EXPECT_EQ(tools[0].name, "my_tool");
  EXPECT_EQ(tools[0].description, "does stuff");
  EXPECT_TRUE(tools[0].parameters.contains("type"));
}

TEST_F(MCPClientTest, ListToolsEmptyResult) {
  MiniHTTPServer server;
  nlohmann::json body = {{"jsonrpc", "2.0"},
                         {"id", 1},
                         {"result", {{"tools", nlohmann::json::array()}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto tools = client.ListTools();

  EXPECT_TRUE(tools.empty());
}

TEST_F(MCPClientTest, ListToolsMultipleTools) {
  MiniHTTPServer server;
  nlohmann::json body = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"result",
       {{"tools",
         {{{"name", "tool_a"}, {"description", "first"}},
          {{"name", "tool_b"}, {"description", "second"}},
          {{"name", "tool_c"}, {"description", "third"}}}}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto tools = client.ListTools();

  ASSERT_EQ(tools.size(), 3u);
  EXPECT_EQ(tools[0].name, "tool_a");
  EXPECT_EQ(tools[1].name, "tool_b");
  EXPECT_EQ(tools[2].name, "tool_c");
}

TEST_F(MCPClientTest, CallToolSuccess) {
  MiniHTTPServer server;
  nlohmann::json body = {
      {"jsonrpc", "2.0"},
      {"id", 2},
      {"result",
       {{"content", {{{"type", "text"}, {"text", "tool output here"}}}}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto resp = client.CallTool("my_tool", {{"input", "hello"}});

  EXPECT_TRUE(resp.error.empty());
  EXPECT_TRUE(resp.result.contains("content"));
  EXPECT_EQ(resp.result["content"][0]["text"], "tool output here");
}

TEST_F(MCPClientTest, CallToolServerError) {
  MiniHTTPServer server;
  nlohmann::json body = {
      {"jsonrpc", "2.0"},
      {"id", 2},
      {"error", {{"code", -32602}, {"message", "Tool not found"}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto resp = client.CallTool("bad_tool", nlohmann::json::object());

  EXPECT_EQ(resp.error, "Tool not found");
  EXPECT_TRUE(resp.result.is_null());
}

// --- inputSchema support ---

TEST_F(MCPClientTest, ListToolsParsesInputSchema) {
  MiniHTTPServer server;
  nlohmann::json body = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"result",
       {{"tools",
         {{{"name", "schema_tool"},
           {"description", "uses inputSchema"},
           {"inputSchema",
            {{"type", "object"},
             {"properties", {{"x", {{"type", "number"}}}}}}}}}}}}};
  int port = server.start(body.dump());
  ASSERT_GT(port, 0);

  MCPClient client("http://127.0.0.1:" + std::to_string(port), logger_);
  auto tools = client.ListTools();

  ASSERT_EQ(tools.size(), 1u);
  EXPECT_EQ(tools[0].name, "schema_tool");
  EXPECT_EQ(tools[0].parameters["type"], "object");
  EXPECT_TRUE(tools[0].parameters["properties"].contains("x"));
}

// --- Struct defaults ---

TEST_F(MCPClientTest, ToolStructDefaults) {
  Tool t;
  EXPECT_TRUE(t.name.empty());
  EXPECT_TRUE(t.description.empty());
  EXPECT_TRUE(t.parameters.is_null());
}

TEST_F(MCPClientTest, MCPResponseDefaults) {
  MCPResponse r;
  EXPECT_TRUE(r.result.is_null());
  EXPECT_TRUE(r.error.empty());
}
