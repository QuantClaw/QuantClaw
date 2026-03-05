// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/web/web_server.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::web {

WebServer::WebServer(int port, std::shared_ptr<spdlog::logger> logger)
    : port_(port), logger_(std::move(logger)), running_(false) {
    logger_->info("WebServer initialized on port {}", port_);
}

WebServer::~WebServer() {
    Stop();
}

void WebServer::AddRoute(const std::string& path, const std::string& method, RequestHandler handler) {
    routes_[path] = {method, std::move(handler)};
    logger_->debug("Added route: {} {}", method, path);
}

void WebServer::AddRawRoute(const std::string& path, const std::string& method, RawHandler handler) {
    raw_routes_.emplace_back(path, method, std::move(handler));
    logger_->debug("Added raw route: {} {}", method, path);
}

void WebServer::EnableCors(const std::string& allowed_origin) {
    cors_enabled_ = true;
    cors_origin_ = allowed_origin;
}

void WebServer::SetAuthToken(const std::string& token) {
    auth_token_ = token;
}

void WebServer::SetMountPoint(const std::string& mount, const std::string& dir) {
    mount_points_.emplace_back(mount, dir);
    logger_->debug("Added mount point: {} -> {}", mount, dir);
}

void WebServer::Start() {
    if (running_) {
        logger_->warn("WebServer already running");
        return;
    }

    running_ = true;
    server_thread_ = std::make_unique<std::thread>(&WebServer::server_loop, this);
    logger_->info("WebServer started on port {}", port_);
}

void WebServer::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (http_server_) {
        http_server_->stop();
    }
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    server_thread_.reset();
    http_server_.reset();
    logger_->info("WebServer stopped");
}

void WebServer::server_loop() {
    http_server_ = std::make_unique<httplib::Server>();

    // CORS: set default headers on all responses
    if (cors_enabled_) {
        httplib::Headers cors_headers = {
            {"Access-Control-Allow-Origin", cors_origin_},
            {"Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
            {"Access-Control-Max-Age", "86400"}
        };
        http_server_->set_default_headers(cors_headers);
    }

    // Auth: pre-routing handler checks Bearer token
    if (!auth_token_.empty()) {
        http_server_->set_pre_routing_handler(
            [this](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
                // Skip auth for health and OPTIONS preflight
                if (req.path == "/health" || req.path == "/api/health" || req.method == "OPTIONS") {
                    return httplib::Server::HandlerResponse::Unhandled;
                }
                // Skip auth for static file paths (UI assets) and control UI
                if (req.path == "/" ||
                    req.path.rfind("/__quantclaw__/control/", 0) == 0 ||
                    req.path.find(".js") != std::string::npos ||
                    req.path.find(".css") != std::string::npos ||
                    req.path.find(".html") != std::string::npos ||
                    req.path.find(".ico") != std::string::npos ||
                    req.path.find(".png") != std::string::npos ||
                    req.path.find(".svg") != std::string::npos ||
                    req.path.find(".woff") != std::string::npos) {
                    return httplib::Server::HandlerResponse::Unhandled;
                }
                std::string auth_header = req.get_header_value("Authorization");
                if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer " &&
                    auth_header.substr(7) == auth_token_) {
                    return httplib::Server::HandlerResponse::Unhandled;  // proceed
                }
                res.status = 401;
                res.set_content(create_error_response("Unauthorized", 401), "application/json");
                return httplib::Server::HandlerResponse::Handled;  // stop
            }
        );
    }

    // CORS preflight catch-all
    if (cors_enabled_) {
        http_server_->Options(".*", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
        });
    }

    // Register simplified routes
    for (const auto& [path, route_info] : routes_) {
        const auto& info = route_info;  // C++17: structured bindings cannot be captured in lambdas
        if (info.method == "GET") {
            http_server_->Get(path, [&info, this](const httplib::Request& req, httplib::Response& res) {
                try {
                    auto body = info.handler("GET", req.body);
                    res.set_content(body, "application/json");
                } catch (const std::exception& e) {
                    logger_->error("Route handler failed: {}", e.what());
                    res.status = 500;
                    res.set_content(create_error_response("Internal server error", 500), "application/json");
                }
            });
        } else if (info.method == "POST") {
            http_server_->Post(path, [&info, this](const httplib::Request& req, httplib::Response& res) {
                try {
                    auto body = info.handler("POST", req.body);
                    res.set_content(body, "application/json");
                } catch (const std::exception& e) {
                    logger_->error("Route handler failed: {}", e.what());
                    res.status = 500;
                    res.set_content(create_error_response("Internal server error", 500), "application/json");
                }
            });
        }
    }

    // Register raw routes (full httplib access)
    for (const auto& [path, method, handler] : raw_routes_) {
        if (method == "GET") {
            http_server_->Get(path, handler);
        } else if (method == "POST") {
            http_server_->Post(path, handler);
        } else if (method == "DELETE") {
            http_server_->Delete(path, handler);
        }
    }

    // Default health endpoint
    http_server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // Mount static file directories
    for (const auto& [mount, dir] : mount_points_) {
        if (http_server_->set_mount_point(mount, dir)) {
            logger_->info("Mounted static files: {} -> {}", mount, dir);
        } else {
            logger_->warn("Failed to mount static files: {} -> {}", mount, dir);
        }
    }

    logger_->info("HTTP server listening on 0.0.0.0:{}", port_);
    http_server_->listen("0.0.0.0", port_);
}

std::string WebServer::create_error_response(const std::string& message, int status_code) {
    nlohmann::json error_response;
    error_response["error"] = message;
    error_response["status"] = status_code;

    return error_response.dump();
}

std::string WebServer::create_success_response(const nlohmann::json& data) {
    nlohmann::json success_response;
    success_response["success"] = true;
    success_response["data"] = data;

    return success_response.dump();
}

} // namespace quantclaw::web
