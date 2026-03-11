// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/tools/tool_registry.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class ToolRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_tools_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    tool_registry_ = std::make_unique<quantclaw::ToolRegistry>(logger_);
    tool_registry_->RegisterBuiltinTools();
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::ToolRegistry> tool_registry_;
};

TEST_F(ToolRegistryTest, AllBuiltinToolsRegistered) {
  auto schemas = tool_registry_->GetToolSchemas();

  EXPECT_FALSE(schemas.empty());

  std::vector<std::string> expected_tools = {"read", "write", "edit", "exec",
                                             "message"};
  for (const auto& tool_name : expected_tools) {
    EXPECT_TRUE(tool_registry_->HasTool(tool_name))
        << "Tool " << tool_name << " not found";
  }
}

TEST_F(ToolRegistryTest, ReadFileTool) {
  auto test_file = test_dir_ / "test.txt";
  std::ofstream file(test_file);
  file << "Hello, QuantClaw!";
  file.close();

  nlohmann::json params = {{"path", test_file.string()}};
  std::string result = tool_registry_->ExecuteTool("read", params);

  EXPECT_EQ(result, "Hello, QuantClaw!");
}

TEST_F(ToolRegistryTest, ReadNonExistentFile) {
  nlohmann::json params = {{"path", (test_dir_ / "nonexistent.txt").string()}};

  EXPECT_THROW(tool_registry_->ExecuteTool("read", params), std::runtime_error);
}

TEST_F(ToolRegistryTest, WriteFileTool) {
  auto test_file = test_dir_ / "output.txt";
  nlohmann::json params = {{"path", test_file.string()},
                           {"content", "This is written by QuantClaw!"}};

  std::string result = tool_registry_->ExecuteTool("write", params);

  EXPECT_TRUE(result.find("Successfully wrote") != std::string::npos);

  std::ifstream file(test_file);
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "This is written by QuantClaw!");
}

TEST_F(ToolRegistryTest, EditFileTool) {
  auto test_file = test_dir_ / "edit.txt";
  std::ofstream file(test_file);
  file << "Original content\nLine 2\nLine 3";
  file.close();

  nlohmann::json params = {{"path", test_file.string()},
                           {"oldText", "Original content"},
                           {"newText", "Modified content"}};

  std::string result = tool_registry_->ExecuteTool("edit", params);

  EXPECT_TRUE(result.find("Successfully edited") != std::string::npos);

  std::ifstream edited_file(test_file);
  std::string content((std::istreambuf_iterator<char>(edited_file)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "Modified content\nLine 2\nLine 3");
}

TEST_F(ToolRegistryTest, EditNonExistentText) {
  auto test_file = test_dir_ / "edit.txt";
  std::ofstream file(test_file);
  file << "Some content";
  file.close();

  nlohmann::json params = {{"path", test_file.string()},
                           {"oldText", "Non-existent text"},
                           {"newText", "New text"}};

  EXPECT_THROW(tool_registry_->ExecuteTool("edit", params), std::runtime_error);
}

TEST_F(ToolRegistryTest, ToolNotFound) {
  nlohmann::json params = {};

  EXPECT_THROW(tool_registry_->ExecuteTool("nonexistent-tool", params),
               std::runtime_error);
}

TEST_F(ToolRegistryTest, MissingParameters) {
  nlohmann::json params = {};

  EXPECT_THROW(tool_registry_->ExecuteTool("read", params), std::runtime_error);
}

// --- exec tool ---

// NOTE: ExecTool direct execution test is intentionally omitted here.
// exec_tool calls SecuritySandbox::apply_resource_limits() which permanently
// lowers RLIMIT_NPROC and RLIMIT_AS for the test process, breaking all
// subsequent WebSocket/thread-based tests. The exec tool is exercised via
// E2E tests running in separate processes.

TEST_F(ToolRegistryTest, ExecToolMissingParam) {
  nlohmann::json params = nlohmann::json::object();
  EXPECT_THROW(tool_registry_->ExecuteTool("exec", params), std::runtime_error);
}

// --- message tool ---

TEST_F(ToolRegistryTest, MessageTool) {
  nlohmann::json params = {{"channel", "test-channel"},
                           {"message", "Hi there"}};
  auto result = tool_registry_->ExecuteTool("message", params);
  EXPECT_NE(result.find("test-channel"), std::string::npos);
}

TEST_F(ToolRegistryTest, MessageToolMissingParams) {
  nlohmann::json params = {{"channel", "x"}};
  EXPECT_THROW(tool_registry_->ExecuteTool("message", params),
               std::runtime_error);
}

// --- write tool missing content ---

TEST_F(ToolRegistryTest, WriteToolMissingContent) {
  nlohmann::json params = {{"path", (test_dir_ / "x.txt").string()}};
  EXPECT_THROW(tool_registry_->ExecuteTool("write", params),
               std::runtime_error);
}

// --- edit tool missing params ---

TEST_F(ToolRegistryTest, EditToolMissingOldText) {
  auto file = test_dir_ / "e.txt";
  std::ofstream f(file);
  f << "content";
  f.close();

  nlohmann::json params = {{"path", file.string()}, {"newText", "x"}};
  EXPECT_THROW(tool_registry_->ExecuteTool("edit", params), std::runtime_error);
}

// --- has_tool ---

TEST_F(ToolRegistryTest, HasToolTrue) {
  EXPECT_TRUE(tool_registry_->HasTool("read"));
  EXPECT_TRUE(tool_registry_->HasTool("write"));
  EXPECT_TRUE(tool_registry_->HasTool("edit"));
  EXPECT_TRUE(tool_registry_->HasTool("exec"));
  EXPECT_TRUE(tool_registry_->HasTool("message"));
}

TEST_F(ToolRegistryTest, HasToolFalse) {
  EXPECT_FALSE(tool_registry_->HasTool("nonexistent"));
  EXPECT_FALSE(tool_registry_->HasTool(""));
}

// --- external tool registration ---

TEST_F(ToolRegistryTest, RegisterExternalTool) {
  tool_registry_->RegisterExternalTool(
      "custom_tool", "A custom tool", nlohmann::json::object(),
      [](const nlohmann::json&) -> std::string { return "custom result"; });

  EXPECT_TRUE(tool_registry_->HasTool("custom_tool"));
  auto result =
      tool_registry_->ExecuteTool("custom_tool", nlohmann::json::object());
  EXPECT_EQ(result, "custom result");
}

// --- chain tool registration ---

TEST_F(ToolRegistryTest, RegisterChainTool) {
  tool_registry_->RegisterChainTool();
  EXPECT_TRUE(tool_registry_->HasTool("chain"));
}

// --- schema count ---

TEST_F(ToolRegistryTest, SchemaCountMatchesRegistered) {
  auto schemas = tool_registry_->GetToolSchemas();
  EXPECT_EQ(schemas.size(),
            12u);  // read, write, edit, exec, bash, apply_patch, process,
                   // message, web_search, web_fetch, memory_search, memory_get

  tool_registry_->RegisterChainTool();
  schemas = tool_registry_->GetToolSchemas();
  EXPECT_EQ(schemas.size(), 13u);
}

// --- schema has name and description ---

TEST_F(ToolRegistryTest, SchemasHaveRequiredFields) {
  auto schemas = tool_registry_->GetToolSchemas();
  for (const auto& schema : schemas) {
    EXPECT_FALSE(schema.name.empty()) << "Schema name is empty";
    EXPECT_FALSE(schema.description.empty())
        << "Schema for " << schema.name << " missing description";
  }
}

// --- empty registry ---

TEST_F(ToolRegistryTest, EmptyRegistryNoTools) {
  auto empty = std::make_unique<quantclaw::ToolRegistry>(logger_);
  EXPECT_TRUE(empty->GetToolSchemas().empty());
  EXPECT_FALSE(empty->HasTool("read"));
}
