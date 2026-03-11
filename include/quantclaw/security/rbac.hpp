// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quantclaw {

// Roles (compatible with OpenClaw connect.hello roles)
enum class Role {
  kOperator,  // Full access: read + write + admin
  kViewer,    // Read-only: list sessions, view history, health
  kNode,      // Machine-to-machine: agent requests, tool execution
};

Role RoleFromString(const std::string& s);
std::string RoleToString(Role r);

// Scope definitions
namespace scopes {
constexpr const char* kOperatorRead = "operator.read";
constexpr const char* kOperatorWrite = "operator.write";
constexpr const char* kOperatorAdmin = "operator.admin";
constexpr const char* kNodeExecute = "node.execute";
constexpr const char* kNodeRead = "node.read";
}  // namespace scopes

// Default scopes per role
std::vector<std::string> DefaultScopes(Role role);

// Method-level access control
class RBACChecker {
 public:
  RBACChecker();

  // Check if a role+scopes combination is allowed to call a method
  bool IsAllowed(const std::string& method, const std::string& role,
                 const std::vector<std::string>& client_scopes) const;

  // Get required scopes for a method (empty = no restriction)
  std::vector<std::string> RequiredScopes(const std::string& method) const;

 private:
  // method → set of allowed scopes (any match = allowed)
  std::unordered_map<std::string, std::unordered_set<std::string>>
      method_scopes_;

  void init_default_rules();
};

}  // namespace quantclaw
