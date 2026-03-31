// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <ixwebsocket/IXWebSocket.h>
#include <spdlog/spdlog.h>

export module quantclaw.gateway.gateway_client;

import std;
import nlohmann.json;

import quantclaw.gateway.protocol;

export namespace quantclaw::gateway {

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

  nlohmann::json Call(const std::string& method,
                      const nlohmann::json& params = {},
                      int timeout_ms = 30000);

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

  struct PendingCall {
    std::mutex mtx;
    std::condition_variable cv;
    std::optional<nlohmann::json> response;
    bool done = false;
  };
  std::mutex pending_mutex_;
  std::unordered_map<std::string, std::shared_ptr<PendingCall>> pending_calls_;

  std::mutex subs_mutex_;
  std::unordered_map<std::string, std::vector<EventCallback>> subscriptions_;

  std::mutex hello_mutex_;
  std::condition_variable hello_cv_;
  bool hello_done_ = false;
  std::string hello_request_id_;
};

}  // namespace quantclaw::gateway
