// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace quantclaw {

struct Message {
  std::string id;
  std::string sender_id;
  std::string content;
  std::string channel_id;
  bool is_group_chat;
};

class Channel {
 public:
  using MessageHandler = std::function<void(const Message&)>;

  virtual ~Channel() = default;

  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual void SendMessage(const std::string& channel_id,
                           const std::string& message) = 0;
  virtual bool IsAllowed(const std::string& sender_id) const = 0;
  virtual std::string GetChannelName() const = 0;

  void SetMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
  }

 protected:
  MessageHandler message_handler_;
};

}  // namespace quantclaw