// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/gateway_server.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace quantclaw::gateway {

GatewayServer::GatewayServer(int port, std::shared_ptr<spdlog::logger> logger)
    : port_(port), logger_(logger) {
  logger_->info("GatewayServer created on port {}", port_);
}

GatewayServer::~GatewayServer() {
  Stop();
}

void GatewayServer::Start() {
  if (running_) {
    logger_->warn("GatewayServer already running");
    return;
  }

  auto ws_server = std::make_unique<HttpAwareWebSocketServer>(port_, "0.0.0.0");
  ws_server->setHttpRedirectPort(http_redirect_port_);
  server_ = std::move(ws_server);

  server_->setOnClientMessageCallback(
      [this](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket& ws,
             const ix::WebSocketMessagePtr& msg) {
        on_connection(state, ws, msg);
      });

  auto res = server_->listen();
  if (!res.first) {
    throw std::runtime_error("Failed to listen on port " +
                             std::to_string(port_) + ": " + res.second);
  }

  server_->start();
  running_ = true;
  start_time_.store(std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());

  logger_->info("GatewayServer started on port {}", port_);
}

void GatewayServer::Stop() {
  if (!running_)
    return;

  running_ = false;
  if (server_) {
    server_->stop();
    server_.reset();
  }

  std::lock_guard<std::mutex> lock(connections_mutex_);
  connections_.clear();
  ws_connections_.clear();

  logger_->info("GatewayServer stopped");
}

bool GatewayServer::IsRunning() const {
  return running_;
}

void GatewayServer::RegisterHandler(const std::string& method,
                                    RpcHandler handler) {
  std::lock_guard<std::mutex> lock(handlers_mutex_);
  handlers_[method] = std::move(handler);
  logger_->debug("Registered RPC handler: {}", method);
}

void GatewayServer::BroadcastEvent(const std::string& event,
                                   const nlohmann::json& payload) {
  RpcEvent evt;
  evt.event = event;
  evt.payload = payload;
  std::string msg = evt.ToJson().dump();

  std::lock_guard<std::mutex> lock(connections_mutex_);
  for (auto& [id, ws] : ws_connections_) {
    if (ws && connections_.count(id)) {
      ws->send(msg);
    }
  }
}

void GatewayServer::SendEventTo(const std::string& connection_id,
                                const RpcEvent& event) {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  if (connections_.count(connection_id) == 0) {
    return;
  }
  auto it = ws_connections_.find(connection_id);
  if (it != ws_connections_.end() && it->second) {
    it->second->send(event.ToJson().dump());
  }
}

void GatewayServer::SendResponseTo(const std::string& connection_id,
                                   const std::string& rpc_request_id, bool ok,
                                   const nlohmann::json& payload_or_error) {
  RpcResponse resp;
  resp.id = rpc_request_id;
  resp.ok = ok;
  if (ok) {
    resp.payload = payload_or_error;
  } else {
    resp.error.message = payload_or_error.value("error", "unknown error");
    resp.error.code = payload_or_error.value("code", "INTERNAL_ERROR");
    resp.error.retryable = payload_or_error.value("retryable", false);
    resp.error.retry_after_ms = payload_or_error.value("retryAfterMs", 0);
  }

  std::lock_guard<std::mutex> lock(connections_mutex_);
  if (connections_.count(connection_id) == 0)
    return;
  auto it = ws_connections_.find(connection_id);
  if (it != ws_connections_.end() && it->second) {
    it->second->send(resp.ToJson().dump());
  }
}

size_t GatewayServer::GetConnectionCount() const {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  return connections_.size();
}

int64_t GatewayServer::GetUptimeSeconds() const {
  auto st = start_time_.load();
  if (!running_ || st == 0) {
    return 0;
  }
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  return now - st;
}

nlohmann::json GatewayServer::BuildSnapshot() const {
  nlohmann::json snapshot;

  // Presence: list of connected, authenticated clients
  nlohmann::json presence = nlohmann::json::array();
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (const auto& [id, client] : connections_) {
      if (!client.authenticated)
        continue;
      nlohmann::json entry;
      entry["connId"] = client.connection_id;
      entry["role"] = client.role;
      entry["scopes"] = client.scopes;
      if (!client.client_name.empty())
        entry["clientName"] = client.client_name;
      if (!client.client_version.empty())
        entry["version"] = client.client_version;
      if (!client.device_id.empty())
        entry["deviceId"] = client.device_id;
      entry["ts"] = client.connected_at;
      presence.push_back(entry);
    }
  }

  snapshot["presence"] = presence;
  snapshot["health"] = nlohmann::json::object();
  snapshot["stateVersion"] = {{"presence", 1}, {"health", 0}};
  snapshot["uptimeMs"] = GetUptimeSeconds() * 1000;

  // Auth mode
  {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    snapshot["authMode"] = auth_mode_;
  }

  // Session defaults
  snapshot["sessionDefaults"] = {{"defaultAgentId", "main"},
                                 {"mainKey", "main"},
                                 {"mainSessionKey", "agent:main:main"}};

  return snapshot;
}

void GatewayServer::on_connection(std::shared_ptr<ix::ConnectionState> state,
                                  ix::WebSocket& ws,
                                  const ix::WebSocketMessagePtr& msg) {
  std::string conn_id = state->getId();

  switch (msg->type) {
    case ix::WebSocketMessageType::Open: {
      logger_->info("Client connected: {}", conn_id);

      {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        ClientConnection client;
        client.connection_id = conn_id;
        client.connected_at =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        connections_[conn_id] = client;
        ws_connections_[conn_id] = &ws;
      }

      // Send challenge
      send_challenge(ws);
      break;
    }

    case ix::WebSocketMessageType::Close: {
      logger_->info("Client disconnected: {}", conn_id);
      std::lock_guard<std::mutex> lock(connections_mutex_);
      connections_.erase(conn_id);
      ws_connections_.erase(conn_id);
      break;
    }

    case ix::WebSocketMessageType::Message: {
      handle_message(conn_id, ws, msg->str);
      break;
    }

    case ix::WebSocketMessageType::Error: {
      logger_->error("WebSocket error for {}: {}", conn_id,
                     msg->errorInfo.reason);
      break;
    }

    default:
      break;
  }
}

void GatewayServer::handle_message(const std::string& conn_id,
                                   ix::WebSocket& ws, const std::string& data) {
  try {
    auto j = nlohmann::json::parse(data);
    auto type = ParseFrameType(j);

    switch (type) {
      case FrameType::kRequest: {
        auto request = RpcRequest::FromJson(j);

        // Special handling for connect.hello / connect (OpenClaw)
        bool is_openclaw = (request.method == methods::kOcConnect);
        if (request.method == methods::kConnectHello || is_openclaw) {
          bool ok = handle_hello(conn_id, request.params, is_openclaw);
          if (ok) {
            HelloOkPayload hello_ok;
            hello_ok.openclaw_format = is_openclaw;
            hello_ok.conn_id = conn_id;
            hello_ok.snapshot = BuildSnapshot();
            auto resp = RpcResponse::success(request.id, hello_ok.ToJson());
            ws.send(resp.ToJson().dump());
          } else {
            auto resp = RpcResponse::failure(
                request.id, "Authentication failed", "AUTH_FAILED");
            ws.send(resp.ToJson().dump());
          }
          return;
        }

        handle_rpc_request(conn_id, ws, request);
        break;
      }

      default:
        logger_->warn("Unexpected frame type from client: {}", data);
        break;
    }
  } catch (const std::exception& e) {
    logger_->error("Failed to parse message from {}: {}", conn_id, e.what());
  }
}

void GatewayServer::handle_rpc_request(const std::string& conn_id,
                                       ix::WebSocket& ws,
                                       const RpcRequest& request) {
  logger_->info("RPC request from {}: {} (id={})", conn_id, request.method,
                request.id);

  RpcHandler handler;
  {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = handlers_.find(request.method);
    if (it == handlers_.end()) {
      auto resp = RpcResponse::failure(
          request.id, "Unknown method: " + request.method, "METHOD_NOT_FOUND");
      ws.send(resp.ToJson().dump());
      return;
    }
    handler = it->second;
  }

  ClientConnection* client = nullptr;
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
      client = &it->second;
    }
  }

  if (!client) {
    auto resp = RpcResponse::failure(request.id, "Connection not found",
                                     "CONNECTION_NOT_FOUND");
    ws.send(resp.ToJson().dump());
    return;
  }

  // Enforce authentication: if auth mode is not "none", client must have sent
  // hello
  std::string current_auth_mode;
  {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    current_auth_mode = auth_mode_;
  }
  if (current_auth_mode != "none" && !client->authenticated) {
    auto resp = RpcResponse::failure(
        request.id, "Not authenticated: send connect.hello first",
        "NOT_AUTHENTICATED");
    ws.send(resp.ToJson().dump());
    return;
  }

  // RBAC check
  if (rbac_checker_ && client->authenticated) {
    if (!rbac_checker_->IsAllowed(request.method, client->role,
                                  client->scopes)) {
      logger_->warn("RBAC denied: method={}, role={}, conn={}", request.method,
                    client->role, conn_id);
      auto resp = RpcResponse::failure(
          request.id,
          "Permission denied: insufficient scope for " + request.method,
          "PERMISSION_DENIED");
      ws.send(resp.ToJson().dump());
      return;
    }
  }

  // Rate limiting check
  if (rate_limiter_) {
    if (!rate_limiter_->Allow(conn_id)) {
      int retry = rate_limiter_->RetryAfter(conn_id);
      logger_->warn("Rate limited: conn={}, retry_after={}s", conn_id, retry);
      auto resp = RpcResponse::failure(request.id,
                                       "Rate limit exceeded. Retry after " +
                                           std::to_string(retry) + "s",
                                       "RATE_LIMITED", true, retry * 1000);
      ws.send(resp.ToJson().dump());
      return;
    }
  }

  try {
    auto result = handler(request.params, *client);
    auto resp = RpcResponse::success(request.id, result);
    ws.send(resp.ToJson().dump());
  } catch (const std::exception& e) {
    logger_->error("RPC handler error for {}: {}", request.method, e.what());
    auto resp = RpcResponse::failure(request.id, e.what(), "HANDLER_ERROR");
    ws.send(resp.ToJson().dump());
  }
}

void GatewayServer::send_challenge(ix::WebSocket& ws) {
  // Generate nonce
  thread_local static std::random_device rd;
  thread_local static std::mt19937 gen(rd());
  thread_local static std::uniform_int_distribution<> dis(0, 15);
  static const char* hex = "0123456789abcdef";

  std::string nonce;
  nonce.reserve(32);
  for (int i = 0; i < 32; ++i) {
    nonce += hex[dis(gen)];
  }

  ConnectChallenge challenge;
  challenge.nonce = nonce;
  challenge.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

  ws.send(challenge.ToJson().dump());
}

void GatewayServer::SetAuth(const std::string& mode, const std::string& token) {
  {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_mode_ = mode;
    expected_token_ = token;
  }
  logger_->info("Gateway auth configured: mode={}", mode);
}

bool GatewayServer::handle_hello(const std::string& conn_id,
                                 const nlohmann::json& params,
                                 bool is_openclaw) {
  auto hello = ConnectHelloParams::FromJson(params);

  // If auth mode is "token", validate the token
  {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    if (auth_mode_ == "token" && !expected_token_.empty()) {
      if (hello.auth_token != expected_token_) {
        logger_->warn("Auth failed for {}: bad token", conn_id);
        return false;
      }
    }
  }
  // auth mode "none" → skip validation

  std::lock_guard<std::mutex> lock(connections_mutex_);
  auto it = connections_.find(conn_id);
  if (it == connections_.end()) {
    return false;
  }

  it->second.role = hello.role;
  // Use client-provided scopes, or fill in defaults based on role
  if (hello.scopes.empty()) {
    it->second.scopes = DefaultScopes(RoleFromString(hello.role));
  } else {
    it->second.scopes = hello.scopes;
  }
  it->second.device_id = hello.device_id;
  it->second.client_name = hello.client_name;
  it->second.client_version = hello.client_version;
  it->second.authenticated = true;
  it->second.client_type = is_openclaw ? "openclaw" : "quantclaw";

  logger_->info("Client {} authenticated: role={}, client={}, type={}", conn_id,
                hello.role, hello.client_name, it->second.client_type);
  return true;
}

}  // namespace quantclaw::gateway
