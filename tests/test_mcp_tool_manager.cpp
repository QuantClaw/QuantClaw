// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/mcp/mcp_tool_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"

#include <gtest/gtest.h>

class MCPToolManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// --- Qualified name construction ---

TEST_F(MCPToolManagerTest, MakeQualifiedName) {
  auto name =
      quantclaw::mcp::MCPToolManager::MakeQualifiedName("code-tools", "lint");
  EXPECT_EQ(name, "mcp__code-tools__lint");
}

TEST_F(MCPToolManagerTest, MakeQualifiedNameSpecialChars) {
  auto name = quantclaw::mcp::MCPToolManager::MakeQualifiedName("my-server",
                                                                "run_tests");
  EXPECT_EQ(name, "mcp__my-server__run_tests");
}

// --- Empty config ---

TEST_F(MCPToolManagerTest, EmptyConfigNoTools) {
  quantclaw::mcp::MCPToolManager manager;
  quantclaw::MCPConfig config;  // No servers

  manager.DiscoverTools(config);
  EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Name resolution ---

TEST_F(MCPToolManagerTest, IsExternalToolFalseForUnknown) {
  quantclaw::mcp::MCPToolManager manager;
  EXPECT_FALSE(manager.IsExternalTool("read"));
  EXPECT_FALSE(manager.IsExternalTool("mcp__unknown__tool"));
}

TEST_F(MCPToolManagerTest, GetServerNameEmptyForUnknown) {
  quantclaw::mcp::MCPToolManager manager;
  EXPECT_EQ(manager.GetServerName("mcp__unknown__tool"), "");
}

TEST_F(MCPToolManagerTest, GetOriginalToolNameEmptyForUnknown) {
  quantclaw::mcp::MCPToolManager manager;
  EXPECT_EQ(manager.GetOriginalToolName("mcp__unknown__tool"), "");
}

// --- Discover tools with invalid server (graceful error) ---

TEST_F(MCPToolManagerTest, DiscoverToolsInvalidServerGraceful) {
  quantclaw::mcp::MCPToolManager manager;
  quantclaw::MCPConfig config;
  quantclaw::MCPServerConfig server;
  server.name = "bad-server";
  server.url = "http://127.0.0.1:1";  // Should fail to connect
  server.timeout = 1;
  config.servers.push_back(server);

  // Should not throw, just log error
  EXPECT_NO_THROW(manager.DiscoverTools(config));
  EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Discover tools skips empty name/url ---

TEST_F(MCPToolManagerTest, DiscoverToolsSkipsEmptyNameOrUrl) {
  quantclaw::mcp::MCPToolManager manager;
  quantclaw::MCPConfig config;

  quantclaw::MCPServerConfig s1;
  s1.name = "";
  s1.url = "http://localhost:9090";
  config.servers.push_back(s1);

  quantclaw::MCPServerConfig s2;
  s2.name = "valid";
  s2.url = "";
  config.servers.push_back(s2);

  EXPECT_NO_THROW(manager.DiscoverTools(config));
  EXPECT_EQ(manager.ToolCount(), 0u);
}

// --- Register into ToolRegistry ---

TEST_F(MCPToolManagerTest, RegisterIntoEmptyManager) {
  quantclaw::mcp::MCPToolManager manager;
  quantclaw::ToolRegistry registry;
  registry.RegisterBuiltinTools();

  auto schemas_before = registry.GetToolSchemas();
  manager.RegisterInto(registry);
  auto schemas_after = registry.GetToolSchemas();

  // No MCP tools discovered, so count should not change
  EXPECT_EQ(schemas_before.size(), schemas_after.size());
}

// --- Execute tool with unknown name ---

TEST_F(MCPToolManagerTest, ExecuteToolUnknownThrows) {
  quantclaw::mcp::MCPToolManager manager;
  EXPECT_THROW(manager.ExecuteTool("mcp__unknown__tool", {}),
               std::runtime_error);
}
