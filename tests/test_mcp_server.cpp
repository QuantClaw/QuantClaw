// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/mcp/mcp_server.hpp"

#include <gtest/gtest.h>

class TestMCPTool : public quantclaw::mcp::MCPTool {
 public:
  TestMCPTool() : MCPTool("test_tool", "A test tool for testing") {
    AddParameter("input", "string", "Input string", true);
  }

 private:
  std::string execute(const nlohmann::json& arguments) override {
    std::string input = arguments.value("input", "");
    return "Processed: " + input;
  }
};

class MCPServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    server_ = std::make_unique<quantclaw::mcp::MCPServer>(logger_);
    server_->RegisterTool(std::make_unique<TestMCPTool>());
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::mcp::MCPServer> server_;
};

TEST_F(MCPServerTest, Initialize) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 1);
  EXPECT_EQ(response["result"]["protocolVersion"], "2024-11-05");
}

TEST_F(MCPServerTest, ListTools) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", {}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 2);
  auto tools = response["result"]["tools"];
  EXPECT_EQ(tools.size(), 1u);
  EXPECT_EQ(tools[0]["name"], "test_tool");
}

TEST_F(MCPServerTest, CallTool) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 3},
      {"method", "tools/call"},
      {"params", {{"name", "test_tool"}, {"arguments", {{"input", "hello"}}}}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 3);
  auto content = response["result"]["content"];
  EXPECT_EQ(content[0]["text"], "Processed: hello");
}

// --- Backward compatibility: old method names still work ---

TEST_F(MCPServerTest, ListToolsLegacyName) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"}, {"id", 10}, {"method", "list_tools"}, {"params", {}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 10);
  EXPECT_TRUE(response.contains("result"));
  EXPECT_EQ(response["result"]["tools"].size(), 1u);
}

TEST_F(MCPServerTest, CallToolLegacyName) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 11},
      {"method", "call_tool"},
      {"params",
       {{"name", "test_tool"}, {"arguments", {{"input", "legacy"}}}}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 11);
  EXPECT_EQ(response["result"]["content"][0]["text"], "Processed: legacy");
}

// --- Tool schema uses inputSchema per MCP spec ---

TEST_F(MCPServerTest, ToolSchemaUsesInputSchema) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"}, {"id", 12}, {"method", "tools/list"}, {"params", {}}};

  auto response = server_->HandleRequest(request);
  auto& tool = response["result"]["tools"][0];

  EXPECT_TRUE(tool.contains("inputSchema"));
  EXPECT_FALSE(tool.contains("parameters"));
  EXPECT_EQ(tool["inputSchema"]["type"], "object");
  EXPECT_TRUE(tool["inputSchema"]["properties"].contains("input"));
}

TEST_F(MCPServerTest, UnknownMethod) {
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 4},
                            {"method", "unknown_method"},
                            {"params", {}}};

  auto response = server_->HandleRequest(request);

  EXPECT_EQ(response["id"], 4);
  EXPECT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32601);
}

TEST_F(MCPServerTest, InitializeAdvertisesResourcesAndPrompts) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"}, {"id", 10}, {"method", "initialize"}, {"params", {}}};
  auto response = server_->HandleRequest(request);
  EXPECT_TRUE(response["result"]["capabilities"].contains("resources"));
  EXPECT_TRUE(response["result"]["capabilities"].contains("prompts"));
}

// --- MCP Resources ---

TEST_F(MCPServerTest, ListResourcesEmpty) {
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 5},
                            {"method", "resources/list"},
                            {"params", {}}};
  auto response = server_->HandleRequest(request);
  EXPECT_TRUE(response["result"]["resources"].is_array());
  EXPECT_EQ(response["result"]["resources"].size(), 0);
}

TEST_F(MCPServerTest, RegisterAndListResource) {
  quantclaw::mcp::MCPResource res;
  res.uri = "file:///workspace/MEMORY.md";
  res.name = "Agent Memory";
  res.description = "Persistent memory file";
  res.mime_type = "text/markdown";
  res.reader = []() { return "# Memory\nsome content"; };
  server_->RegisterResource(std::move(res));

  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 6},
                            {"method", "resources/list"},
                            {"params", {}}};
  auto response = server_->HandleRequest(request);
  auto& resources = response["result"]["resources"];
  ASSERT_EQ(resources.size(), 1);
  EXPECT_EQ(resources[0]["uri"], "file:///workspace/MEMORY.md");
  EXPECT_EQ(resources[0]["name"], "Agent Memory");
  EXPECT_EQ(resources[0]["mimeType"], "text/markdown");
}

TEST_F(MCPServerTest, ReadResource) {
  quantclaw::mcp::MCPResource res;
  res.uri = "file:///test/data.txt";
  res.name = "Test Data";
  res.mime_type = "text/plain";
  res.reader = []() { return "hello world"; };
  server_->RegisterResource(std::move(res));

  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 7},
                            {"method", "resources/read"},
                            {"params", {{"uri", "file:///test/data.txt"}}}};
  auto response = server_->HandleRequest(request);
  EXPECT_FALSE(response.contains("error"));
  auto& contents = response["result"]["contents"];
  ASSERT_EQ(contents.size(), 1);
  EXPECT_EQ(contents[0]["text"], "hello world");
  EXPECT_EQ(contents[0]["uri"], "file:///test/data.txt");
}

TEST_F(MCPServerTest, ReadResourceNotFound) {
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 8},
                            {"method", "resources/read"},
                            {"params", {{"uri", "file:///nonexistent"}}}};
  auto response = server_->HandleRequest(request);
  EXPECT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32602);
}

// --- MCP Prompts ---

TEST_F(MCPServerTest, ListPromptsEmpty) {
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 9},
                            {"method", "prompts/list"},
                            {"params", {}}};
  auto response = server_->HandleRequest(request);
  EXPECT_TRUE(response["result"]["prompts"].is_array());
  EXPECT_EQ(response["result"]["prompts"].size(), 0);
}

TEST_F(MCPServerTest, RegisterAndListPrompt) {
  quantclaw::mcp::MCPPrompt prompt;
  prompt.name = "summarize";
  prompt.description = "Summarize text";
  prompt.arguments = {{"text", "Text to summarize", true}};
  prompt.renderer = [](const nlohmann::json& args) -> nlohmann::json {
    std::string text = args.value("text", "");
    return {{{"role", "user"}, {"content", "Please summarize: " + text}}};
  };
  server_->RegisterPrompt(std::move(prompt));

  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 11},
                            {"method", "prompts/list"},
                            {"params", {}}};
  auto response = server_->HandleRequest(request);
  auto& prompts = response["result"]["prompts"];
  ASSERT_EQ(prompts.size(), 1);
  EXPECT_EQ(prompts[0]["name"], "summarize");
  EXPECT_EQ(prompts[0]["description"], "Summarize text");
  ASSERT_EQ(prompts[0]["arguments"].size(), 1);
  EXPECT_EQ(prompts[0]["arguments"][0]["name"], "text");
  EXPECT_TRUE(prompts[0]["arguments"][0]["required"]);
}

TEST_F(MCPServerTest, GetPrompt) {
  quantclaw::mcp::MCPPrompt prompt;
  prompt.name = "greet";
  prompt.description = "Greeting prompt";
  prompt.renderer = [](const nlohmann::json& args) -> nlohmann::json {
    std::string name = args.value("name", "World");
    return {{{"role", "user"}, {"content", "Hello, " + name + "!"}}};
  };
  server_->RegisterPrompt(std::move(prompt));

  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", 12},
      {"method", "prompts/get"},
      {"params", {{"name", "greet"}, {"arguments", {{"name", "Alice"}}}}}};
  auto response = server_->HandleRequest(request);
  EXPECT_FALSE(response.contains("error"));
  auto& messages = response["result"]["messages"];
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"], "Hello, Alice!");
}

TEST_F(MCPServerTest, GetPromptNotFound) {
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 13},
                            {"method", "prompts/get"},
                            {"params", {{"name", "nonexistent"}}}};
  auto response = server_->HandleRequest(request);
  EXPECT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32602);
}
