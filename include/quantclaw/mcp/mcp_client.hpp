// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::mcp {

struct Tool {
  std::string name;
  std::string description;
  nlohmann::json parameters;
};

struct MCPResponse {
  nlohmann::json result;
  std::string error;
};

class MCPClient {
 private:
  std::string server_url_;
  std::shared_ptr<spdlog::logger> logger_;
  int request_id_ = 0;

 public:
  MCPClient(const std::string& server_url,
            std::shared_ptr<spdlog::logger> logger);

  std::vector<Tool> ListTools();
  MCPResponse CallTool(const std::string& tool_name,
                       const nlohmann::json& arguments);

 private:
  nlohmann::json make_request(const nlohmann::json& request);
};

}  // namespace quantclaw::mcp