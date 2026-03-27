// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import "quantclaw/cli/session_commands.hpp";

import std;

import "quantclaw/gateway/gateway_client.hpp";

namespace quantclaw::cli {

SessionCommands::SessionCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {}

int SessionCommands::ListCommand(const std::vector<std::string>& args) {
  bool json_output = false;
  int limit = 20;

  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--json") {
      json_output = true;
    } else if (arg == "--limit" && i + 1 < args.size()) {
      limit = std::stoi(args[i + 1]);
      ++i;  // Skip the value argument
    }
  }

  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect()) {
      std::cerr << "Error: Cannot connect to gateway" << std::endl;
      return 1;
    }

    auto result = client->Call("sessions.list", {{"limit", limit}});

    // RPC returns {sessions:[...], count, ...}; extract the array
    nlohmann::json sessions_arr = nlohmann::json::array();
    if (result.is_object() && result.contains("sessions")) {
      sessions_arr = result["sessions"];
    } else if (result.is_array()) {
      sessions_arr = result;
    }

    if (json_output) {
      std::cout << sessions_arr.dump(2) << std::endl;
    } else {
      if (sessions_arr.empty()) {
        std::cout << "No sessions found" << std::endl;
      } else {
        std::cout << std::left << std::setw(35) << "KEY" << std::setw(15)
                  << "ID" << std::setw(25) << "UPDATED" << std::setw(20)
                  << "NAME" << std::endl;
        std::cout << std::string(95, '-') << std::endl;

        for (const auto& session : sessions_arr) {
          // updatedAt may be a string ISO timestamp or a number (ms epoch)
          std::string updated_at;
          if (session.contains("updatedAt")) {
            if (session["updatedAt"].is_string()) {
              updated_at = session["updatedAt"].get<std::string>();
            } else if (session["updatedAt"].is_number()) {
              updated_at =
                  std::to_string(session["updatedAt"].get<long long>());
            }
          }
          std::cout << std::left << std::setw(35) << session.value("key", "")
                    << std::setw(15) << session.value("sessionId", "")
                    << std::setw(25) << updated_at << std::setw(20)
                    << session.value("displayName", "") << std::endl;
        }
      }
    }

    client->Disconnect();
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

int SessionCommands::HistoryCommand(const std::vector<std::string>& args) {
  std::string session_key;
  bool json_output = false;
  int limit = -1;

  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--json") {
      json_output = true;
    } else if (arg == "--limit" && i + 1 < args.size()) {
      limit = std::stoi(args[i + 1]);
      ++i;  // Skip the value argument
    } else if (arg[0] != '-' && session_key.empty()) {
      session_key = arg;
    }
  }

  if (session_key.empty()) {
    std::cerr << "Error: session key required" << std::endl;
    std::cerr << "Usage: quantclaw sessions history <session-key>" << std::endl;
    return 1;
  }

  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect()) {
      std::cerr << "Error: Cannot connect to gateway" << std::endl;
      return 1;
    }

    nlohmann::json params = {{"sessionKey", session_key}};
    if (limit > 0)
      params["limit"] = limit;

    auto result = client->Call("sessions.history", params);

    if (json_output) {
      std::cout << result.dump(2) << std::endl;
    } else if (result.is_array()) {
      for (const auto& msg : result) {
        std::string role = msg.value("role", "unknown");
        std::string ts = msg.value("timestamp", "");

        // content may be a string or an array of ContentBlocks
        std::string content;
        if (msg.contains("content")) {
          const auto& c = msg["content"];
          if (c.is_string()) {
            content = c.get<std::string>();
          } else if (c.is_array()) {
            for (const auto& block : c) {
              if (block.is_object() && block.value("type", "") == "text") {
                if (!content.empty()) {
                  content += "\n";
                }
                content += block.value("text", "");
              }
            }
          }
        }

        if (role == "user") {
          std::cout << "\033[36m[" << ts << "] User:\033[0m " << content
                    << std::endl;
        } else if (role == "assistant") {
          std::cout << "\033[32m[" << ts << "] Assistant:\033[0m " << content
                    << std::endl;
        } else {
          std::cout << "[" << ts << "] " << role << ": " << content
                    << std::endl;
        }
        std::cout << std::endl;
      }
    }

    client->Disconnect();
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

int SessionCommands::DeleteCommand(const std::vector<std::string>& args) {
  std::string session_key;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i][0] != '-' && session_key.empty()) {
      session_key = args[i];
    }
  }

  if (session_key.empty()) {
    std::cerr << "Error: session key required" << std::endl;
    std::cerr << "Usage: quantclaw sessions delete <session-key>" << std::endl;
    return 1;
  }

  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect()) {
      std::cerr << "Error: Cannot connect to gateway" << std::endl;
      return 1;
    }

    auto result =
        client->Call("sessions.delete", {{"sessionKey", session_key}});
    client->Disconnect();

    std::cout << "Session deleted: " << session_key << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

int SessionCommands::ResetCommand(const std::vector<std::string>& args) {
  std::string session_key;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i][0] != '-' && session_key.empty()) {
      session_key = args[i];
    }
  }

  if (session_key.empty()) {
    std::cerr << "Error: session key required" << std::endl;
    std::cerr << "Usage: quantclaw sessions reset <session-key>" << std::endl;
    return 1;
  }

  try {
    auto client = std::make_shared<gateway::GatewayClient>(
        gateway_url_, auth_token_, logger_);
    if (!client->Connect()) {
      std::cerr << "Error: Cannot connect to gateway" << std::endl;
      return 1;
    }

    auto result = client->Call("sessions.reset", {{"sessionKey", session_key}});
    client->Disconnect();

    std::cout << "Session reset: " << session_key << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

}  // namespace quantclaw::cli
