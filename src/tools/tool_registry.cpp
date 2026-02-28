// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/security/sandbox.hpp"
#include "quantclaw/security/tool_permissions.hpp"
#include "quantclaw/security/exec_approval.hpp"
#include "quantclaw/core/subagent.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/mcp/mcp_tool_manager.hpp"
#include "quantclaw/platform/process.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

namespace quantclaw {

ToolRegistry::ToolRegistry(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
    logger_->info("ToolRegistry initialized");
}

void ToolRegistry::RegisterBuiltinTools() {
    // Register file system tools with security sandbox
    tools_["read"] = [this](const nlohmann::json& params) {
        return read_file_tool(params);
    };
    tool_schemas_.push_back({
        "read",
        "Read the contents of a file",
        R"({"type": "object", "properties": {"path": {"type": "string", "description": "Path to the file to read"}}, "required": ["path"]})"
    });

    tools_["write"] = [this](const nlohmann::json& params) {
        return write_file_tool(params);
    };
    tool_schemas_.push_back({
        "write",
        "Write content to a file",
        R"({"type": "object", "properties": {"path": {"type": "string", "description": "Path to the file to write"}, "content": {"type": "string", "description": "Content to write to the file"}}, "required": ["path", "content"]})"
    });

    tools_["edit"] = [this](const nlohmann::json& params) {
        return edit_file_tool(params);
    };
    tool_schemas_.push_back({
        "edit",
        "Edit a file by replacing exact text",
        R"({"type": "object", "properties": {"path": {"type": "string", "description": "Path to the file to edit"}, "oldText": {"type": "string", "description": "Exact text to find and replace"}, "newText": {"type": "string", "description": "New text to replace the old text with"}}, "required": ["path", "oldText", "newText"]})"
    });

    // Register shell execution tool with security sandbox
    tools_["exec"] = [this](const nlohmann::json& params) {
        return exec_tool(params);
    };
    tool_schemas_.push_back({
        "exec",
        "Execute shell commands",
        R"({"type": "object", "properties": {"command": {"type": "string", "description": "Shell command to execute"}}, "required": ["command"]})"
    });

    // Register message tool
    tools_["message"] = [this](const nlohmann::json& params) {
        return message_tool(params);
    };
    tool_schemas_.push_back({
        "message",
        "Send messages to channels",
        R"({"type": "object", "properties": {"channel": {"type": "string", "description": "Channel to send message to"}, "message": {"type": "string", "description": "Message content to send"}}, "required": ["channel", "message"]})"
    });

    logger_->info("Registered {} built-in tools", tools_.size());
}

void ToolRegistry::RegisterExternalTool(const std::string& name,
                                           const std::string& description,
                                           const nlohmann::json& parameters,
                                           std::function<std::string(const nlohmann::json&)> executor) {
    tools_[name] = std::move(executor);
    tool_schemas_.push_back({name, description, parameters});
    external_tools_.insert(name);
    logger_->info("Registered external tool: {}", name);
}

void ToolRegistry::RegisterChainTool() {
    tools_["chain"] = [this](const nlohmann::json& params) -> std::string {
        auto chain_def = ToolChainExecutor::ParseChain(params);
        ToolExecutorFn executor = [this](const std::string& name, const nlohmann::json& args) {
            return ExecuteTool(name, args);
        };
        ToolChainExecutor chain_executor(executor, logger_);
        auto result = chain_executor.Execute(chain_def);
        return ToolChainExecutor::ResultToJson(result).dump();
    };

    nlohmann::json chain_params;
    chain_params["type"] = "object";
    chain_params["properties"] = {
        {"name", {{"type", "string"}, {"description", "Name of the chain"}}},
        {"steps", {
            {"type", "array"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"tool", {{"type", "string"}, {"description", "Tool name to execute"}}},
                    {"arguments", {{"type", "object"}, {"description", "Arguments for the tool, may contain {{prev.result}} or {{steps[N].result}} templates"}}}
                }},
                {"required", {"tool"}}
            }},
            {"description", "Ordered list of tool invocations"}
        }},
        {"error_policy", {{"type", "string"}, {"enum", {"stop_on_error", "continue_on_error", "retry"}}, {"description", "How to handle step failures"}}},
        {"max_retries", {{"type", "integer"}, {"description", "Max retries per step (only used with retry policy)"}}}
    };
    chain_params["required"] = {"steps"};

    tool_schemas_.push_back({"chain",
        "Execute a pipeline of tools in sequence. Each step can reference previous results via {{prev.result}} or {{steps[N].result}} templates.",
        chain_params
    });

    logger_->info("Registered chain tool");
}

void ToolRegistry::SetPermissionChecker(std::shared_ptr<ToolPermissionChecker> checker) {
    permission_checker_ = std::move(checker);
    logger_->info("Permission checker set on ToolRegistry");
}

void ToolRegistry::SetMcpToolManager(std::shared_ptr<mcp::MCPToolManager> manager) {
    mcp_tool_manager_ = std::move(manager);
    logger_->info("MCP tool manager set on ToolRegistry");
}

void ToolRegistry::SetApprovalManager(std::shared_ptr<ExecApprovalManager> manager) {
    approval_manager_ = std::move(manager);
    logger_->info("Exec approval manager set on ToolRegistry");
}

void ToolRegistry::SetSubagentManager(SubagentManager* manager,
                                         const std::string& session_key) {
    subagent_manager_ = manager;
    current_session_key_ = session_key;

    if (!manager) return;

    // Register spawn_subagent tool
    tools_["spawn_subagent"] = [this](const nlohmann::json& params) -> std::string {
        if (!subagent_manager_) {
            throw std::runtime_error("Subagent manager not configured");
        }

        SpawnParams sp;
        sp.task = params.value("task", "");
        if (sp.task.empty()) {
            throw std::runtime_error("Missing required parameter: task");
        }
        sp.label = params.value("label", "");
        sp.agent_id = params.value("agent_id", "");
        sp.model = params.value("model", "");
        sp.thinking = params.value("thinking", "off");
        sp.timeout_seconds = params.value("timeout", 300);
        sp.mode = spawn_mode_from_string(params.value("mode", "run"));
        sp.cleanup = params.value("cleanup", true);

        auto result = subagent_manager_->Spawn(sp, current_session_key_);

        nlohmann::json r;
        r["status"] = (result.status == SpawnResult::kAccepted) ? "accepted" :
                      (result.status == SpawnResult::kForbidden) ? "forbidden" : "error";
        if (!result.child_session_key.empty())
            r["child_session_key"] = result.child_session_key;
        if (!result.run_id.empty())
            r["run_id"] = result.run_id;
        r["mode"] = spawn_mode_to_string(result.mode);
        if (!result.note.empty()) r["note"] = result.note;
        if (!result.error.empty()) r["error"] = result.error;
        return r.dump();
    };

    nlohmann::json spawn_params;
    spawn_params["type"] = "object";
    spawn_params["properties"] = {
        {"task", {{"type", "string"}, {"description", "Task description for the subagent"}}},
        {"label", {{"type", "string"}, {"description", "Human-readable label"}}},
        {"agent_id", {{"type", "string"}, {"description", "Target agent ID"}}},
        {"model", {{"type", "string"}, {"description", "Model override (e.g. openai/gpt-4)"}}},
        {"thinking", {{"type", "string"}, {"description", "Thinking level: off|low|medium|high"}}},
        {"timeout", {{"type", "integer"}, {"description", "Timeout in seconds"}}},
        {"mode", {{"type", "string"}, {"enum", {"run", "session"}}, {"description", "run=ephemeral, session=persistent"}}},
        {"cleanup", {{"type", "boolean"}, {"description", "Auto-delete session on completion"}}}
    };
    spawn_params["required"] = {"task"};

    // Remove existing schema if re-registering
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "spawn_subagent"; }),
        tool_schemas_.end());

    tool_schemas_.push_back({
        "spawn_subagent",
        "Spawn a subagent to handle a subtask autonomously. The subagent gets its own session and can use all tools.",
        spawn_params
    });

    logger_->info("Subagent manager set, spawn_subagent tool registered");
}

bool ToolRegistry::check_permission(const std::string& tool_name) const {
    if (!permission_checker_) {
        return true;  // No checker = allow all
    }

    // Check if this is an external (MCP) tool
    if (external_tools_.count(tool_name) && mcp_tool_manager_) {
        std::string server_name = mcp_tool_manager_->GetServerName(tool_name);
        std::string original_name = mcp_tool_manager_->GetOriginalToolName(tool_name);
        return permission_checker_->IsMcpToolAllowed(server_name, original_name);
    }

    return permission_checker_->IsAllowed(tool_name);
}

std::string ToolRegistry::ExecuteTool(const std::string& tool_name,
                                     const nlohmann::json& parameters) {
    if (!HasTool(tool_name)) {
        throw std::runtime_error("Tool not found: " + tool_name);
    }

    // Permission check
    if (!check_permission(tool_name)) {
        throw std::runtime_error("Permission denied: tool '" + tool_name + "' is not allowed");
    }

    logger_->debug("Executing tool: {} with parameters: {}", tool_name, parameters.dump());

    try {
        auto result = tools_[tool_name](parameters);
        logger_->debug("Tool execution successful");
        return result;
    } catch (const std::exception& e) {
        logger_->error("Tool execution failed: {}", e.what());
        throw;
    }
}

std::vector<ToolRegistry::ToolSchema> ToolRegistry::GetToolSchemas() const {
    if (!permission_checker_) {
        return tool_schemas_;
    }

    std::vector<ToolSchema> filtered;
    for (const auto& schema : tool_schemas_) {
        if (external_tools_.count(schema.name) && mcp_tool_manager_) {
            std::string server_name = mcp_tool_manager_->GetServerName(schema.name);
            std::string original_name = mcp_tool_manager_->GetOriginalToolName(schema.name);
            if (permission_checker_->IsMcpToolAllowed(server_name, original_name)) {
                filtered.push_back(schema);
            }
        } else {
            if (permission_checker_->IsAllowed(schema.name)) {
                filtered.push_back(schema);
            }
        }
    }
    return filtered;
}

bool ToolRegistry::HasTool(const std::string& tool_name) const {
    return tools_.find(tool_name) != tools_.end();
}

std::string ToolRegistry::read_file_tool(const nlohmann::json& params) {
    if (!params.contains("path")) {
        throw std::runtime_error("Missing required parameter: path");
    }

    std::string path = params["path"].get<std::string>();

    // Apply security sandbox - validate path
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace")) {
        throw std::runtime_error("Access denied: Path outside allowed workspace: " + path);
    }

    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File not found: " + path);
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    return content.str();
}

std::string ToolRegistry::write_file_tool(const nlohmann::json& params) {
    if (!params.contains("path") || !params.contains("content")) {
        throw std::runtime_error("Missing required parameters: path, content");
    }

    std::string path = params["path"].get<std::string>();
    std::string content = params["content"].get<std::string>();

    // Apply security sandbox - validate path
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace")) {
        throw std::runtime_error("Access denied: Path outside allowed workspace: " + path);
    }

    // Create directory if it doesn't exist
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to write file: " + path);
    }

    file << content;
    file.close();

    return "Successfully wrote to file: " + path;
}

std::string ToolRegistry::edit_file_tool(const nlohmann::json& params) {
    if (!params.contains("path") || !params.contains("oldText") || !params.contains("newText")) {
        throw std::runtime_error("Missing required parameters: path, oldText, newText");
    }

    std::string path = params["path"].get<std::string>();
    std::string old_text = params["oldText"].get<std::string>();
    std::string new_text = params["newText"].get<std::string>();

    // Apply security sandbox - validate path
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace")) {
        throw std::runtime_error("Access denied: Path outside allowed workspace: " + path);
    }

    // Read the file
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for editing: " + path);
    }

    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    std::string file_content = content.str();

    // Find and replace
    size_t pos = file_content.find(old_text);
    if (pos == std::string::npos) {
        throw std::runtime_error("Text not found in file: " + old_text);
    }

    file_content.replace(pos, old_text.length(), new_text);

    // Write back to file
    std::ofstream out_file(path);
    if (!out_file.is_open()) {
        throw std::runtime_error("Failed to write edited file: " + path);
    }

    out_file << file_content;
    out_file.close();

    return "Successfully edited file: " + path;
}

std::string ToolRegistry::exec_tool(const nlohmann::json& params) {
    if (!params.contains("command")) {
        throw std::runtime_error("Missing required parameter: command");
    }

    std::string command = params["command"].get<std::string>();

    // Apply security sandbox - validate command
    if (!quantclaw::SecuritySandbox::ValidateShellCommand(command)) {
        throw std::runtime_error("Command not allowed: " + command);
    }

    // Exec approval check (if approval manager is configured)
    if (approval_manager_) {
        auto decision = approval_manager_->RequestApproval(command);
        if (decision == ApprovalDecision::kDenied) {
            throw std::runtime_error("Command execution denied by approval policy: " + command);
        }
        if (decision == ApprovalDecision::kTimeout) {
            throw std::runtime_error("Command execution approval timed out: " + command);
        }
    }

    // Apply resource limits
    quantclaw::SecuritySandbox::ApplyResourceLimits();

    logger_->info("Executing command: {}", command);

    auto result = platform::exec_capture(command, 30);
    if (result.exit_code == -1) {
        throw std::runtime_error("Failed to execute command: " + command);
    }
    if (result.exit_code == -2) {
        throw std::runtime_error("Command timeout exceeded: " + command);
    }
    if (result.exit_code != 0) {
        throw std::runtime_error("Command failed with exit code " + std::to_string(result.exit_code));
    }

    return result.output;
}

std::string ToolRegistry::message_tool(const nlohmann::json& params) {
    if (!params.contains("channel") || !params.contains("message")) {
        throw std::runtime_error("Missing required parameters: channel, message");
    }

    std::string channel = params["channel"].get<std::string>();
    std::string message = params["message"].get<std::string>();

    logger_->info("Sending message to channel {}: {}", channel, message);

    return "Message sent to channel: " + channel;
}

} // namespace quantclaw
