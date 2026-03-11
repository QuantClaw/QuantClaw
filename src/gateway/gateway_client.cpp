// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/gateway_client.hpp"

#include <chrono>
#include <sstream>

namespace quantclaw::gateway {

GatewayClient::GatewayClient(const std::string& url, const std::string& token,
                             std::shared_ptr<spdlog::logger> logger)
    : url_(url), token_(token), logger_(logger) {}

GatewayClient::~GatewayClient() {
  Disconnect();
}

bool GatewayClient::Connect(int timeout_ms) {
  ws_.setUrl(url_);

  ws_.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr& msg) { on_message(msg); });

  ws_.start();

  // Wait for connection
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  // Give the background thread time to start connecting
  // (initial state is Closed before it transitions to Connecting)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (!connected_ && std::chrono::steady_clock::now() < deadline) {
    auto state = ws_.getReadyState();
    if (state == ix::ReadyState::Open) {
      connected_ = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (!connected_) {
    logger_->error("Connection timeout to {}", url_);
    ws_.stop();
    return false;
  }

  // Wait for hello handshake to complete
  {
    std::unique_lock<std::mutex> lock(hello_mutex_);
    hello_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [this] { return hello_done_; });
  }

  if (!authenticated_) {
    logger_->warn("Connected but not yet authenticated");
  }

  logger_->info("Connected to gateway at {}", url_);
  return true;
}

void GatewayClient::Disconnect() {
  if (connected_) {
    ws_.stop();
    connected_ = false;
    authenticated_ = false;
    logger_->info("Disconnected from gateway");
  }
}

bool GatewayClient::IsConnected() const {
  return connected_;
}

nlohmann::json GatewayClient::Call(const std::string& method,
                                   const nlohmann::json& params,
                                   int timeout_ms) {
  if (!connected_) {
    throw std::runtime_error("Not connected to gateway");
  }

  std::string req_id = next_request_id();

  // Create pending call
  auto pending = std::make_shared<PendingCall>();
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_calls_[req_id] = pending;
  }

  // Send request
  RpcRequest request;
  request.id = req_id;
  request.method = method;
  request.params = params;
  ws_.send(request.ToJson().dump());

  // Wait for response
  {
    std::unique_lock<std::mutex> lock(pending->mtx);
    bool ok = pending->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   [&] { return pending->done; });
    if (!ok) {
      std::lock_guard<std::mutex> plock(pending_mutex_);
      pending_calls_.erase(req_id);
      throw std::runtime_error("RPC call timeout: " + method);
    }
  }

  // Clean up
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_calls_.erase(req_id);
  }

  if (!pending->response) {
    throw std::runtime_error("No response received for: " + method);
  }

  auto& resp = *pending->response;
  if (resp.contains("ok") && !resp["ok"].get<bool>()) {
    std::string error_msg = "unknown error";
    if (resp.contains("error")) {
      if (resp["error"].is_object()) {
        error_msg = resp["error"].value("message", "unknown error");
      } else if (resp["error"].is_string()) {
        error_msg = resp["error"].get<std::string>();
      }
    }
    throw std::runtime_error("RPC error: " + error_msg);
  }

  return resp.value("payload", nlohmann::json::object());
}

void GatewayClient::Subscribe(const std::string& event,
                              EventCallback callback) {
  std::lock_guard<std::mutex> lock(subs_mutex_);
  subscriptions_[event].push_back(std::move(callback));
}

void GatewayClient::on_message(const ix::WebSocketMessagePtr& msg) {
  switch (msg->type) {
    case ix::WebSocketMessageType::Open:
      connected_ = true;
      logger_->debug("WebSocket connection opened");
      break;

    case ix::WebSocketMessageType::Close:
      connected_ = false;
      authenticated_ = false;
      logger_->info("WebSocket connection closed");
      break;

    case ix::WebSocketMessageType::Message: {
      try {
        auto j = nlohmann::json::parse(msg->str);
        handle_frame(j);
      } catch (const std::exception& e) {
        logger_->error("Failed to parse message: {}", e.what());
      }
      break;
    }

    case ix::WebSocketMessageType::Error:
      logger_->error("WebSocket error: {}", msg->errorInfo.reason);
      break;

    default:
      break;
  }
}

void GatewayClient::handle_frame(const nlohmann::json& frame) {
  auto type_str = frame.value("type", "");

  if (type_str == "event") {
    auto event_name = frame.value("event", "");
    auto payload = frame.value("payload", nlohmann::json::object());

    // Handle challenge -> send hello
    if (event_name == events::kConnectChallenge) {
      RpcRequest hello;
      hello.id = next_request_id();
      hello.method = methods::kConnectHello;
      hello.params = {{"minProtocol", 1},
                      {"maxProtocol", 3},
                      {"clientName", "quantclaw-cli"},
                      {"clientVersion", "0.2.0"},
                      {"role", "operator"},
                      {"scopes", {"operator.read", "operator.write"}},
                      {"authToken", token_}};
      ws_.send(hello.ToJson().dump());
      return;
    }

    // Dispatch to subscribers
    std::lock_guard<std::mutex> lock(subs_mutex_);
    auto it = subscriptions_.find(event_name);
    if (it != subscriptions_.end()) {
      for (auto& cb : it->second) {
        cb(event_name, payload);
      }
    }
    // Also dispatch to wildcard subscribers
    auto wild = subscriptions_.find("*");
    if (wild != subscriptions_.end()) {
      for (auto& cb : wild->second) {
        cb(event_name, payload);
      }
    }
  } else if (type_str == "res") {
    auto id = frame.value("id", "");

    // Check if this is the hello response
    if (frame.value("ok", false)) {
      auto payload = frame.value("payload", nlohmann::json::object());
      if (payload.contains("protocol")) {
        authenticated_ = true;
        {
          std::lock_guard<std::mutex> lock(hello_mutex_);
          hello_done_ = true;
        }
        hello_cv_.notify_all();
      }
    }

    // Resolve pending call
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_calls_.find(id);
    if (it != pending_calls_.end()) {
      std::lock_guard<std::mutex> plock(it->second->mtx);
      it->second->response = frame;
      it->second->done = true;
      it->second->cv.notify_all();
    }
  }
}

std::string GatewayClient::next_request_id() {
  return std::to_string(++request_counter_);
}

}  // namespace quantclaw::gateway
