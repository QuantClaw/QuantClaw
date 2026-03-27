// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.security.tool_permissions;

import std;
import quantclaw.config;

export namespace quantclaw {

class ToolPermissionChecker {
 public:
  explicit ToolPermissionChecker(const ToolPermissionConfig& config);

  bool IsAllowed(const std::string& tool_name) const;
  bool IsMcpToolAllowed(const std::string& server_name,
                        const std::string& tool_name) const;

 private:
  void expand_groups();

  std::unordered_set<std::string> allowed_tools_;
  std::unordered_set<std::string> denied_tools_;
  std::unordered_set<std::string> allowed_mcp_;
  std::unordered_set<std::string> denied_mcp_;
  bool allow_all_ = false;
  bool mcp_allow_all_ = false;
};

}  // namespace quantclaw