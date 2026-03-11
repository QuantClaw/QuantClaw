// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "quantclaw/mcp/mcp_client.hpp"
#include "quantclaw/mcp/mcp_server.hpp"

namespace quantclaw::cli {

class MCPCommands {
 private:
  std::shared_ptr<spdlog::logger> logger_;

 public:
  explicit MCPCommands(std::shared_ptr<spdlog::logger> logger);

  // MCP Server commands
  int McpServerStartCommand(const std::vector<std::string>& args);
  int McpServerStatusCommand(const std::vector<std::string>& args);

  // MCP Client commands
  int McpClientListToolsCommand(const std::vector<std::string>& args);
  int McpClientCallToolCommand(const std::vector<std::string>& args);

 private:
  void ParseServerArgs(const std::vector<std::string>& args, int& port,
                       std::string& host);
  void ParseClientArgs(const std::vector<std::string>& args,
                       std::string& server_url);
};

}  // namespace quantclaw::cli