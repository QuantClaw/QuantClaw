// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/gateway/protocol.hpp"

namespace quantclaw::gateway {

class GatewayClient {
 public:
  using EventCallback = std::function<void(const std::string& event,
                                           const nlohmann::json& payload)>;

  GatewayClient(const std::string& url, const std::string& token,
                std::shared_ptr<spdlog::logger> logger);
  ~GatewayClient();

  bool Connect(int timeout_ms = 5000);
  void Disconnect();
  bool IsConnected() const;

  // Synchronous RPC call (sends req, waits for res)
  nlohmann::json Call(const std::string& method,
                      const nlohmann::json& params = {},
                      int timeout_ms = 30000);

  // Subscribe to events
  void Subscribe(const std::string& event, EventCallback callback);

 private:
  void on_message(const ix::WebSocketMessagePtr& msg);
  void handle_frame(const nlohmann::json& frame);
  std::string next_request_id();

  std::string url_;
  std::string token_;
  std::shared_ptr<spdlog::logger> logger_;
  ix::WebSocket ws_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> authenticated_{false};
  std::atomic<uint64_t> request_counter_{0};

  // Pending RPC responses
  struct PendingCall {
    std::mutex mtx;
    std::condition_variable cv;
    std::optional<nlohmann::json> response;
    bool done = false;
  };
  std::mutex pending_mutex_;
  std::unordered_map<std::string, std::shared_ptr<PendingCall>> pending_calls_;

  // Event subscriptions
  std::mutex subs_mutex_;
  std::unordered_map<std::string, std::vector<EventCallback>> subscriptions_;

  // Hello handshake
  std::mutex hello_mutex_;
  std::condition_variable hello_cv_;
  bool hello_done_ = false;
};

}  // namespace quantclaw::gateway
