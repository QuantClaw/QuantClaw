// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.cli.agent_commands;

import std;

import quantclaw.constants;

export namespace quantclaw::cli {

class AgentCommands {
 public:
  explicit AgentCommands(std::shared_ptr<spdlog::logger> logger);

  int RequestCommand(const std::vector<std::string>& args);
  int StopCommand(const std::vector<std::string>& args);

  void SetGatewayUrl(const std::string& url) {
    gateway_url_ = url;
  }
  void SetAuthToken(const std::string& token) {
    auth_token_ = token;
  }
  void SetDefaultTimeoutMs(int ms) {
    if (ms > 0) {
      default_timeout_ms_ = ms;
    }
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::string gateway_url_ = kDefaultGatewayUrl;
  std::string auth_token_;
  int default_timeout_ms_ = 120000;
};

}  // namespace quantclaw::cli