// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <unordered_set>

#include "quantclaw/config.hpp"

namespace quantclaw {

class ToolPermissionChecker {
 public:
  explicit ToolPermissionChecker(const ToolPermissionConfig& config);

  // Check if a built-in tool is allowed
  bool IsAllowed(const std::string& tool_name) const;

  // Check if an MCP tool is allowed (qualified name: mcp__server__tool)
  bool IsMcpToolAllowed(const std::string& server_name,
                        const std::string& tool_name) const;

 private:
  void expand_groups();

  std::unordered_set<std::string> allowed_tools_;
  std::unordered_set<std::string> denied_tools_;
  // MCP allow/deny: "server_name:*" or "server_name:tool_name"
  std::unordered_set<std::string> allowed_mcp_;
  std::unordered_set<std::string> denied_mcp_;
  bool allow_all_ = false;
  bool mcp_allow_all_ = false;
};

}  // namespace quantclaw
