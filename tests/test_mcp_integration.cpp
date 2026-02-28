// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include "quantclaw/mcp/mcp_server.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

// Simple MCP tool backed by ToolRegistry
class RegistryBackedTool : public quantclaw::mcp::MCPTool {
public:
    RegistryBackedTool(const std::string& name, const std::string& description,
                       quantclaw::ToolRegistry* registry)
        : MCPTool(name, description), registry_(registry) {}

private:
    std::string execute(const nlohmann::json& arguments) override {
        return registry_->ExecuteTool(GetName(), arguments);
    }
    quantclaw::ToolRegistry* registry_;
};

class MCPIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "quantclaw_mcp_integ_test";
        std::filesystem::create_directories(test_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("test", null_sink);

        tool_registry_ = std::make_unique<quantclaw::ToolRegistry>(logger_);
        tool_registry_->RegisterBuiltinTools();

        mcp_server_ = std::make_unique<quantclaw::mcp::MCPServer>(logger_);

        // Register tools from tool registry into MCP server
        auto schemas = tool_registry_->GetToolSchemas();
        for (const auto& schema : schemas) {
            mcp_server_->RegisterTool(
                std::make_unique<RegistryBackedTool>(schema.name, schema.description, tool_registry_.get()));
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    std::filesystem::path test_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<quantclaw::ToolRegistry> tool_registry_;
    std::unique_ptr<quantclaw::mcp::MCPServer> mcp_server_;
};

TEST_F(MCPIntegrationTest, ListTools) {
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", "list_tools"},
        {"id", 1}
    };

    auto response = mcp_server_->HandleRequest(request);

    EXPECT_TRUE(response.contains("result"));
    EXPECT_TRUE(response["result"].contains("tools"));
    EXPECT_EQ(response["result"]["tools"].size(), 5u); // read, write, edit, exec, message
}

TEST_F(MCPIntegrationTest, CallReadFileTool) {
    auto test_file = test_dir_ / "test_read.txt";
    std::ofstream f(test_file);
    f << "Hello from MCP integration test!";
    f.close();

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", "call_tool"},
        {"params", {
            {"name", "read"},
            {"arguments", {{"path", test_file.string()}}}
        }},
        {"id", 2}
    };

    auto response = mcp_server_->HandleRequest(request);

    EXPECT_TRUE(response.contains("result"));
    EXPECT_TRUE(response["result"].contains("content"));
    EXPECT_EQ(response["result"]["content"][0]["text"], "Hello from MCP integration test!");
}

TEST_F(MCPIntegrationTest, CallWriteFileTool) {
    auto test_file = test_dir_ / "test_write.txt";

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", "call_tool"},
        {"params", {
            {"name", "write"},
            {"arguments", {
                {"path", test_file.string()},
                {"content", "Written by MCP integration test"}
            }}
        }},
        {"id", 3}
    };

    auto response = mcp_server_->HandleRequest(request);

    EXPECT_TRUE(response.contains("result"));

    std::ifstream written_file(test_file);
    std::string content((std::istreambuf_iterator<char>(written_file)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "Written by MCP integration test");
}

TEST_F(MCPIntegrationTest, ToolNotFound) {
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", "call_tool"},
        {"params", {
            {"name", "nonexistent_tool"},
            {"arguments", nlohmann::json::object()}
        }},
        {"id", 4}
    };

    auto response = mcp_server_->HandleRequest(request);

    EXPECT_TRUE(response.contains("error"));
}
