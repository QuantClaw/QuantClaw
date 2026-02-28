// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "quantclaw/config.hpp"

namespace quantclaw {

class ToolPermissionChecker;
class ExecApprovalManager;
class SubagentManager;

namespace mcp {
    class MCPToolManager;
}

class ToolRegistry {
public:
    struct ToolSchema {
        std::string name;
        std::string description;
        nlohmann::json parameters;
    };

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::unordered_map<std::string, std::function<std::string(const nlohmann::json&)>> tools_;
    std::vector<ToolSchema> tool_schemas_;
    std::shared_ptr<ToolPermissionChecker> permission_checker_;
    std::shared_ptr<mcp::MCPToolManager> mcp_tool_manager_;
    std::shared_ptr<ExecApprovalManager> approval_manager_;
    SubagentManager* subagent_manager_ = nullptr;       // Non-owning, optional
    std::string current_session_key_;                     // For subagent context
    std::unordered_set<std::string> external_tools_;  // tracks MCP tool names

public:
    explicit ToolRegistry(std::shared_ptr<spdlog::logger> logger);

    // Register built-in tools (compatible with OpenClaw)
    void RegisterBuiltinTools();

    // Register an external tool (from MCP server)
    void RegisterExternalTool(const std::string& name,
                              const std::string& description,
                              const nlohmann::json& parameters,
                              std::function<std::string(const nlohmann::json&)> executor);

    // Register the chain meta-tool
    void RegisterChainTool();

    // Set permission checker (filters GetToolSchemas and ExecuteTool)
    void SetPermissionChecker(std::shared_ptr<ToolPermissionChecker> checker);

    // Set MCP tool manager (for permission checks on external tools)
    void SetMcpToolManager(std::shared_ptr<mcp::MCPToolManager> manager);

    // Set exec approval manager (for exec tool approval flow)
    void SetApprovalManager(std::shared_ptr<ExecApprovalManager> manager);

    // Set subagent manager and register spawn_subagent tool
    void SetSubagentManager(SubagentManager* manager,
                            const std::string& session_key = "");

    // Execute a tool by name (with permission check)
    std::string ExecuteTool(const std::string& tool_name,
                            const nlohmann::json& parameters);

    // Get tool schemas for LLM function calling (filtered by permissions)
    std::vector<ToolSchema> GetToolSchemas() const;

    // Check if tool is available
    bool HasTool(const std::string& tool_name) const;

private:
    // Permission check helper
    bool check_permission(const std::string& tool_name) const;

    // Built-in tool implementations
    std::string read_file_tool(const nlohmann::json& params);
    std::string write_file_tool(const nlohmann::json& params);
    std::string edit_file_tool(const nlohmann::json& params);
    std::string exec_tool(const nlohmann::json& params);
    std::string message_tool(const nlohmann::json& params);
};

} // namespace quantclaw
