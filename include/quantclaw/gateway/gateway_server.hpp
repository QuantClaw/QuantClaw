// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttp.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/security/rbac.hpp"
#include "quantclaw/security/rate_limiter.hpp"

namespace quantclaw::gateway {

// WebSocket server that handles plain HTTP requests gracefully
// instead of logging "Missing Sec-WebSocket-Key" errors.
// When a browser or HTTP client hits the WS port, it returns a redirect
// to the Control UI HTTP port.
class HttpAwareWebSocketServer : public ix::WebSocketServer {
public:
    using ix::WebSocketServer::WebSocketServer;

    void setHttpRedirectPort(int port) { http_port_ = port; }

private:
    int http_port_ = 0;

    void handleConnection(std::unique_ptr<ix::Socket> socket,
                          std::shared_ptr<ix::ConnectionState> connectionState) override
    {
        // Parse the incoming HTTP request from the raw socket
        auto [ok, errMsg, request] = ix::Http::parseRequest(socket, getHandshakeTimeoutSecs());
        if (!ok) {
            // Malformed request — just close
            connectionState->setTerminated();
            return;
        }

        // Check if this is a proper WebSocket upgrade request
        bool has_ws_key = request->headers.find("sec-websocket-key") != request->headers.end();
        if (has_ws_key) {
            // Real WebSocket client — proceed with the normal upgrade path,
            // passing the already-parsed request so bytes aren't re-read.
            handleUpgrade(std::move(socket), connectionState, request);
            connectionState->setTerminated();
            return;
        }

        // Plain HTTP request (browser, curl, health-check, etc.)
        auto resp = std::make_shared<ix::HttpResponse>();
        if (http_port_ > 0) {
            resp->statusCode = 301;
            resp->description = "Moved Permanently";
            resp->headers["Location"] = "http://localhost:" + std::to_string(http_port_) + "/";
            resp->headers["Content-Type"] = "text/html";
            resp->body = "<html><body>Redirecting to <a href=\"http://localhost:"
                + std::to_string(http_port_) + "/\">dashboard</a></body></html>\n";
        } else {
            resp->statusCode = 426;
            resp->description = "Upgrade Required";
            resp->headers["Upgrade"] = "websocket";
            resp->headers["Content-Type"] = "text/plain";
            resp->body = "This is a WebSocket endpoint. Use a WebSocket client to connect.\n";
        }
        ix::Http::sendResponse(resp, socket);
        connectionState->setTerminated();
    }
};

using RpcHandler = std::function<nlohmann::json(const nlohmann::json& params, ClientConnection& client)>;

class GatewayServer {
public:
    GatewayServer(int port, std::shared_ptr<spdlog::logger> logger);
    ~GatewayServer();

    void Start();
    void Stop();
    bool IsRunning() const;

    void RegisterHandler(const std::string& method, RpcHandler handler);
    void BroadcastEvent(const std::string& event, const nlohmann::json& payload);
    void SendEventTo(const std::string& connection_id, const RpcEvent& event);

    int GetPort() const { return port_; }
    size_t GetConnectionCount() const;
    int64_t GetUptimeSeconds() const;

    // Configure authentication
    void SetAuth(const std::string& mode, const std::string& token);
    std::string GetAuthMode() const {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        return auth_mode_;
    }

    // Set HTTP port for redirect when plain HTTP hits the WS port
    void SetHttpRedirectPort(int port) { http_redirect_port_ = port; }

    // Enable RBAC enforcement
    void SetRbac(std::shared_ptr<RBACChecker> checker) { rbac_checker_ = std::move(checker); }

    // Enable rate limiting
    void SetRateLimiter(std::shared_ptr<RateLimiter> limiter) { rate_limiter_ = std::move(limiter); }

private:
    void on_connection(std::shared_ptr<ix::ConnectionState> state,
                       ix::WebSocket& ws,
                       const ix::WebSocketMessagePtr& msg);
    void handle_message(const std::string& conn_id,
                        ix::WebSocket& ws,
                        const std::string& data);
    void handle_rpc_request(const std::string& conn_id,
                            ix::WebSocket& ws,
                            const RpcRequest& request);
    void send_challenge(ix::WebSocket& ws);
    bool handle_hello(const std::string& conn_id,
                      const nlohmann::json& params,
                      bool is_openclaw = false);

    int port_;
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> start_time_{0};

    // Auth config (protected by auth_mutex_)
    mutable std::mutex auth_mutex_;
    std::string auth_mode_ = "token";
    std::string expected_token_;

    // HTTP redirect target (Control UI port)
    int http_redirect_port_ = 0;

    mutable std::mutex connections_mutex_;
    std::unordered_map<std::string, ClientConnection> connections_;
    // Non-owning WebSocket pointers. Lifetime managed by ixwebsocket:
    // valid from Open callback until Close callback. Always access under
    // connections_mutex_ and verify key exists in connections_ first.
    std::unordered_map<std::string, ix::WebSocket*> ws_connections_;

    std::mutex handlers_mutex_;
    std::unordered_map<std::string, RpcHandler> handlers_;

    // Security
    std::shared_ptr<RBACChecker> rbac_checker_;
    std::shared_ptr<RateLimiter> rate_limiter_;
};

} // namespace quantclaw::gateway
