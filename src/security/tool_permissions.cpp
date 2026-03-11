// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/tool_permissions.hpp"

#include <algorithm>

namespace quantclaw {

// Group definitions
static const std::unordered_map<std::string, std::vector<std::string>> kGroups =
    {
        {"fs", {"read", "write", "edit"}},
        {"runtime", {"exec"}},
        {"all", {"read", "write", "edit", "exec", "message"}},
};

ToolPermissionChecker::ToolPermissionChecker(
    const ToolPermissionConfig& config) {
  // Parse allow list
  for (const auto& entry : config.allow) {
    if (entry.substr(0, 6) == "group:") {
      std::string group_name = entry.substr(6);
      if (group_name == "all") {
        allow_all_ = true;
      }
      auto it = kGroups.find(group_name);
      if (it != kGroups.end()) {
        for (const auto& tool : it->second) {
          allowed_tools_.insert(tool);
        }
      }
    } else if (entry.substr(0, 5) == "tool:") {
      allowed_tools_.insert(entry.substr(5));
    } else if (entry.substr(0, 4) == "mcp:") {
      std::string mcp_spec = entry.substr(4);
      if (mcp_spec == "*") {
        mcp_allow_all_ = true;
      } else {
        allowed_mcp_.insert(mcp_spec);
      }
    }
  }

  // Parse deny list
  for (const auto& entry : config.deny) {
    if (entry.substr(0, 6) == "group:") {
      std::string group_name = entry.substr(6);
      auto it = kGroups.find(group_name);
      if (it != kGroups.end()) {
        for (const auto& tool : it->second) {
          denied_tools_.insert(tool);
        }
      }
    } else if (entry.substr(0, 5) == "tool:") {
      denied_tools_.insert(entry.substr(5));
    } else if (entry.substr(0, 4) == "mcp:") {
      std::string mcp_spec = entry.substr(4);
      denied_mcp_.insert(mcp_spec);
    }
  }

  // If allow list is empty (no entries at all), allow everything
  if (config.allow.empty()) {
    allow_all_ = true;
    mcp_allow_all_ = true;
  }
}

bool ToolPermissionChecker::IsAllowed(const std::string& tool_name) const {
  // Deny takes priority over allow
  if (denied_tools_.count(tool_name)) {
    return false;
  }

  // If allow_all or tool is in allowed set
  if (allow_all_ || allowed_tools_.count(tool_name)) {
    return true;
  }

  return false;
}

bool ToolPermissionChecker::IsMcpToolAllowed(
    const std::string& server_name, const std::string& tool_name) const {
  // Check deny first: exact match then wildcard
  std::string exact = server_name + ":" + tool_name;
  std::string wildcard = server_name + ":*";

  if (denied_mcp_.count(exact) || denied_mcp_.count(wildcard)) {
    return false;
  }

  // Check allow: exact, wildcard, or allow_all
  if (mcp_allow_all_ || allowed_mcp_.count(exact) ||
      allowed_mcp_.count(wildcard)) {
    return true;
  }

  return false;
}

}  // namespace quantclaw
