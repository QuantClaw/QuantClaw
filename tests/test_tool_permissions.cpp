// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/tool_permissions.hpp"

#include <gtest/gtest.h>

class ToolPermissionsTest : public ::testing::Test {
 protected:
  quantclaw::ToolPermissionConfig
  make_config(const std::vector<std::string>& allow,
              const std::vector<std::string>& deny) {
    quantclaw::ToolPermissionConfig cfg;
    cfg.allow = allow;
    cfg.deny = deny;
    return cfg;
  }
};

// --- Basic allow/deny ---

TEST_F(ToolPermissionsTest, EmptyConfigAllowsAll) {
  auto checker = quantclaw::ToolPermissionChecker(make_config({}, {}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_TRUE(checker.IsAllowed("exec"));
  EXPECT_TRUE(checker.IsAllowed("anything"));
}

TEST_F(ToolPermissionsTest, GroupFsAllowsFileTools) {
  auto checker =
      quantclaw::ToolPermissionChecker(make_config({"group:fs"}, {}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_TRUE(checker.IsAllowed("edit"));
  EXPECT_FALSE(checker.IsAllowed("exec"));
  EXPECT_FALSE(checker.IsAllowed("message"));
}

TEST_F(ToolPermissionsTest, GroupRuntimeAllowsExec) {
  auto checker =
      quantclaw::ToolPermissionChecker(make_config({"group:runtime"}, {}));
  EXPECT_TRUE(checker.IsAllowed("exec"));
  EXPECT_FALSE(checker.IsAllowed("read"));
  EXPECT_FALSE(checker.IsAllowed("write"));
}

TEST_F(ToolPermissionsTest, GroupAllAllowsEverything) {
  auto checker =
      quantclaw::ToolPermissionChecker(make_config({"group:all"}, {}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_TRUE(checker.IsAllowed("edit"));
  EXPECT_TRUE(checker.IsAllowed("exec"));
  EXPECT_TRUE(checker.IsAllowed("message"));
}

TEST_F(ToolPermissionsTest, SingleToolAllow) {
  auto checker =
      quantclaw::ToolPermissionChecker(make_config({"tool:read"}, {}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_FALSE(checker.IsAllowed("write"));
  EXPECT_FALSE(checker.IsAllowed("exec"));
}

TEST_F(ToolPermissionsTest, DenyOverridesAllow) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"group:all"}, {"tool:exec"}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_FALSE(checker.IsAllowed("exec"));
}

TEST_F(ToolPermissionsTest, DenyGroupOverridesAllowGroup) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"group:all"}, {"group:runtime"}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_FALSE(checker.IsAllowed("exec"));
}

TEST_F(ToolPermissionsTest, MultipleAllowGroups) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"group:fs", "group:runtime"}, {}));
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_TRUE(checker.IsAllowed("edit"));
  EXPECT_TRUE(checker.IsAllowed("exec"));
  EXPECT_FALSE(checker.IsAllowed("message"));
}

// --- MCP tool permissions ---

TEST_F(ToolPermissionsTest, McpAllowAllWhenConfigEmpty) {
  auto checker = quantclaw::ToolPermissionChecker(make_config({}, {}));
  EXPECT_TRUE(checker.IsMcpToolAllowed("code-tools", "lint"));
  EXPECT_TRUE(checker.IsMcpToolAllowed("data", "query"));
}

TEST_F(ToolPermissionsTest, McpAllowSpecificServer) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"group:all", "mcp:code-tools:*"}, {}));
  EXPECT_TRUE(checker.IsMcpToolAllowed("code-tools", "lint"));
  EXPECT_TRUE(checker.IsMcpToolAllowed("code-tools", "format"));
  EXPECT_FALSE(checker.IsMcpToolAllowed("data", "query"));
}

TEST_F(ToolPermissionsTest, McpAllowSpecificTool) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"group:all", "mcp:code-tools:lint"}, {}));
  EXPECT_TRUE(checker.IsMcpToolAllowed("code-tools", "lint"));
  EXPECT_FALSE(checker.IsMcpToolAllowed("code-tools", "format"));
}

TEST_F(ToolPermissionsTest, McpDenyOverridesAllow) {
  auto checker = quantclaw::ToolPermissionChecker(make_config(
      {"group:all", "mcp:code-tools:*"}, {"mcp:code-tools:dangerous"}));
  EXPECT_TRUE(checker.IsMcpToolAllowed("code-tools", "lint"));
  EXPECT_FALSE(checker.IsMcpToolAllowed("code-tools", "dangerous"));
}

TEST_F(ToolPermissionsTest, McpDenyEntireServer) {
  auto checker = quantclaw::ToolPermissionChecker(
      make_config({"mcp:*"}, {"mcp:untrusted:*"}));
  EXPECT_TRUE(checker.IsMcpToolAllowed("trusted", "anything"));
  EXPECT_FALSE(checker.IsMcpToolAllowed("untrusted", "anything"));
}

// --- Default config (FromJson defaults) ---

TEST_F(ToolPermissionsTest, DefaultPermissionConfigFromJson) {
  nlohmann::json empty_json = nlohmann::json::object();
  auto cfg = quantclaw::ToolPermissionConfig::FromJson(empty_json);
  // Default: allow group:fs and group:runtime
  auto checker = quantclaw::ToolPermissionChecker(cfg);
  EXPECT_TRUE(checker.IsAllowed("read"));
  EXPECT_TRUE(checker.IsAllowed("write"));
  EXPECT_TRUE(checker.IsAllowed("edit"));
  EXPECT_TRUE(checker.IsAllowed("exec"));
  EXPECT_FALSE(checker.IsAllowed("message"));
}
