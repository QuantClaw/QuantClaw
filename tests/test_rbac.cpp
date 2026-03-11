// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/rbac.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

TEST(RBAC, RoleConversion) {
  EXPECT_EQ(RoleFromString("operator"), Role::kOperator);
  EXPECT_EQ(RoleFromString("viewer"), Role::kViewer);
  EXPECT_EQ(RoleFromString("node"), Role::kNode);
  EXPECT_EQ(RoleFromString("unknown"), Role::kOperator);  // default

  EXPECT_EQ(RoleToString(Role::kOperator), "operator");
  EXPECT_EQ(RoleToString(Role::kViewer), "viewer");
  EXPECT_EQ(RoleToString(Role::kNode), "node");
}

TEST(RBAC, DefaultScopes) {
  auto op = DefaultScopes(Role::kOperator);
  EXPECT_EQ(op.size(), 3u);

  auto viewer = DefaultScopes(Role::kViewer);
  EXPECT_EQ(viewer.size(), 1u);
  EXPECT_EQ(viewer[0], scopes::kOperatorRead);

  auto node = DefaultScopes(Role::kNode);
  EXPECT_EQ(node.size(), 2u);
}

TEST(RBAC, OperatorFullAccess) {
  RBACChecker checker;
  auto op_scopes = DefaultScopes(Role::kOperator);

  // Operator can do everything
  EXPECT_TRUE(checker.IsAllowed("gateway.health", "operator", op_scopes));
  EXPECT_TRUE(checker.IsAllowed("config.set", "operator", op_scopes));
  EXPECT_TRUE(checker.IsAllowed("agent.request", "operator", op_scopes));
  EXPECT_TRUE(checker.IsAllowed("sessions.delete", "operator", op_scopes));
  EXPECT_TRUE(checker.IsAllowed("models.set", "operator", op_scopes));
}

TEST(RBAC, ViewerReadOnly) {
  RBACChecker checker;
  auto viewer_scopes = DefaultScopes(Role::kViewer);

  // Viewer can read
  EXPECT_TRUE(checker.IsAllowed("gateway.health", "viewer", viewer_scopes));
  EXPECT_TRUE(checker.IsAllowed("gateway.status", "viewer", viewer_scopes));
  EXPECT_TRUE(checker.IsAllowed("sessions.list", "viewer", viewer_scopes));
  EXPECT_TRUE(checker.IsAllowed("sessions.history", "viewer", viewer_scopes));
  EXPECT_TRUE(checker.IsAllowed("channels.list", "viewer", viewer_scopes));

  // Viewer cannot write
  EXPECT_FALSE(checker.IsAllowed("config.set", "viewer", viewer_scopes));
  EXPECT_FALSE(checker.IsAllowed("sessions.delete", "viewer", viewer_scopes));
  EXPECT_FALSE(checker.IsAllowed("agent.request", "viewer", viewer_scopes));
  EXPECT_FALSE(checker.IsAllowed("models.set", "viewer", viewer_scopes));
}

TEST(RBAC, NodeExecuteOnly) {
  RBACChecker checker;
  auto node_scopes = DefaultScopes(Role::kNode);

  // Node can read
  EXPECT_TRUE(checker.IsAllowed("gateway.health", "node", node_scopes));
  EXPECT_TRUE(checker.IsAllowed("sessions.list", "node", node_scopes));

  // Node can execute
  EXPECT_TRUE(checker.IsAllowed("agent.request", "node", node_scopes));
  EXPECT_TRUE(checker.IsAllowed("chain.execute", "node", node_scopes));

  // Node cannot admin
  EXPECT_FALSE(checker.IsAllowed("config.set", "node", node_scopes));
  EXPECT_FALSE(checker.IsAllowed("sessions.delete", "node", node_scopes));
  EXPECT_FALSE(checker.IsAllowed("models.set", "node", node_scopes));
}

TEST(RBAC, ConnectAlwaysAllowed) {
  RBACChecker checker;

  // connect.hello is always allowed, even with no scopes
  EXPECT_TRUE(checker.IsAllowed("connect.hello", "operator", {}));
  EXPECT_TRUE(checker.IsAllowed("connect", "operator", {}));
}

TEST(RBAC, UnknownMethodRequiresAdmin) {
  RBACChecker checker;

  // Unknown methods require operator.admin
  EXPECT_TRUE(checker.IsAllowed("some.future.method", "operator",
                                {scopes::kOperatorAdmin}));
  EXPECT_FALSE(checker.IsAllowed("some.future.method", "operator",
                                 {scopes::kOperatorRead}));
}
