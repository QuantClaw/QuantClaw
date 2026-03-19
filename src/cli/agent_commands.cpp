// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/cli/agent_commands.hpp"

#include <chrono>
#include <iostream>

#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/protocol.hpp"

namespace quantclaw::cli {

AgentCommands::AgentCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {}

int AgentCommands::RequestCommand(const std::vector<std::string>& args) {
  std::string message;
  std::string session_key = "agent:main:main";
  std::string model;
  bool json_output = false;
  bool no_session = false;
  int timeout_ms = default_timeout_ms_;

  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if ((arg == "-m" || arg == "--message") && i + 1 < args.size()) {
      message = args[i + 1];
      ++i;  // Skip the value argument
    } else if ((arg == "-s" || arg == "--session" || arg == "--session-id") &&
               i + 1 < args.size()) {
      session_key = args[i + 1];
      ++i;  // Skip the value argument
    } else if (arg == "--model" && i + 1 < args.size()) {
      model = args[i + 1];
      ++i;  // Skip the value argument
    } else if (arg == "--timeout" && i + 1 < args.size()) {
      timeout_ms = std::stoi(args[i + 1]) * 1000;
      ++i;  // Skip the value argument
    } else if (arg == "--json") {
      json_output = true;
    } else if (arg == "--no-session") {
      no_session = true;
    } else if (arg[0] != '-' && message.empty()) {
      message = arg;
    }
  }

  // --no-session: generate ephemeral session key so no history persists
  if (no_session) {
    session_key =
        "ephemeral:" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
  }

  if (message.empty()) {
    std::cerr
        << "Usage: quantclaw agent -m \"your message\" [--session-id <id>] "
           "[--timeout <seconds>] [--json]"
        << std::endl;
    return 1;
  }

  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect(timeout_ms)) {
      std::cerr << "Error: Cannot connect to gateway at " << gateway_url_
                << std::endl;
      std::cerr << "Is the gateway running? Start it with: quantclaw gateway"
                << std::endl;
      return 1;
    }

    // Subscribe to streaming events (print text deltas in real-time)
    if (!json_output) {
      client->Subscribe("agent.text_delta", [](const std::string&,
                                               const nlohmann::json& payload) {
        if (payload.contains("text")) {
          std::cout << payload["text"].get<std::string>() << std::flush;
        }
      });
      client->Subscribe("agent.message_end",
                        [](const std::string&, const nlohmann::json&) {
                          std::cout << std::endl;
                        });
    }

    nlohmann::json params = {{"sessionKey", session_key}, {"message", message}};
    if (!model.empty()) {
      params["model"] = model;
    }

    auto result = client->Call("agent.request", params, timeout_ms);

    if (json_output) {
      std::cout << result.dump(2) << std::endl;
    }

    client->Disconnect();
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

int AgentCommands::StopCommand(const std::vector<std::string>& /*args*/) {
  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect()) {
      std::cerr << "Error: Cannot connect to gateway" << std::endl;
      return 1;
    }

    auto result = client->Call("agent.stop", {});
    std::cout << "Agent stopped" << std::endl;

    client->Disconnect();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

}  // namespace quantclaw::cli
