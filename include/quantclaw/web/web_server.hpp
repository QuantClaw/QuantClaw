// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::web {

class WebServer {
 public:
  using RequestHandler =
      std::function<std::string(const std::string&, const std::string&)>;
  using RawHandler =
      std::function<void(const httplib::Request&, httplib::Response&)>;

  WebServer(int port, std::shared_ptr<spdlog::logger> logger);
  ~WebServer();

  // Simplified route — handler receives (method, body) and returns response
  // string
  void AddRoute(const std::string& path, const std::string& method,
                RequestHandler handler);

  // Raw route — handler receives full httplib Request/Response (query params,
  // headers, etc.)
  void AddRawRoute(const std::string& path, const std::string& method,
                   RawHandler handler);

  // Enable CORS headers on all responses
  void EnableCors(const std::string& allowed_origin = "*");

  // Set bearer token for auth (empty = no auth check)
  void SetAuthToken(const std::string& token);

  // Mount a directory for static file serving
  void SetMountPoint(const std::string& mount, const std::string& dir);

  void Start();
  void Stop();

 private:
  struct RouteInfo {
    std::string method;
    RequestHandler handler;
  };

  int port_;
  std::shared_ptr<spdlog::logger> logger_;
  std::atomic<bool> running_;
  std::unique_ptr<std::thread> server_thread_;
  std::unordered_map<std::string, RouteInfo> routes_;
  std::vector<std::tuple<std::string, std::string, RawHandler>> raw_routes_;
  std::unique_ptr<httplib::Server> http_server_;

  bool cors_enabled_ = false;
  std::string cors_origin_;
  std::string auth_token_;
  std::vector<std::pair<std::string, std::string>> mount_points_;

  void server_loop();
  std::string create_error_response(const std::string& message,
                                    int status_code);
  std::string create_success_response(const nlohmann::json& data);
};

}  // namespace quantclaw::web
