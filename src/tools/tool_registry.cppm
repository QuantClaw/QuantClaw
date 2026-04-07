// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.tools.tool_registry;

import std;
import nlohmann.json;
import quantclaw.core.cron_scheduler;
import quantclaw.core.subagent;
import quantclaw.mcp.mcp_tool_manager;
import quantclaw.security.exec_approval;
import quantclaw.security.scope_validator;
import quantclaw.security.tool_permissions;
import quantclaw.session.session_manager;
import quantclaw.core.recon_runtime;

export namespace quantclaw {

class ToolRegistry {
 public:
	struct ToolSchema {
		std::string name;
		std::string description;
		std::string parameters_json;
	};

	// Background process session (for 'process' tool)
	struct BgSession {
		std::string id;
		std::string command;
		std::future<std::string> future;  // captured output (or error)
		std::atomic<bool> done{false};
		std::string output;  // final output once done
		std::string error;
		int exit_code = -1;
		std::chrono::system_clock::time_point started_at;
		bool exited = false;
	};

 private:
	std::shared_ptr<spdlog::logger> logger_;
	std::unordered_map<std::string,
										 std::function<std::string(const nlohmann::json&)>>
			tools_;
	std::vector<ToolSchema> tool_schemas_;
	std::shared_ptr<ToolPermissionChecker> permission_checker_;
	std::shared_ptr<mcp::MCPToolManager> mcp_tool_manager_;
	std::shared_ptr<ExecApprovalManager> approval_manager_;
	SubagentManager* subagent_manager_ = nullptr;
	std::string current_session_key_;
	std::unordered_set<std::string> external_tools_;

	// Optional subsystems wired in at startup
	std::shared_ptr<CronScheduler> cron_scheduler_;
	std::shared_ptr<SessionManager> session_manager_;

	// Background process registry (for 'process' tool)
	mutable std::mutex bg_mu_;
	std::unordered_map<std::string, std::shared_ptr<BgSession>> bg_sessions_;

	// Workspace root for file-tool path validation
	std::string workspace_path_ = "~/.quantclaw/workspace";

	// Recon mode: scope enforcement and findings graph
	std::shared_ptr<ScopeValidator> scope_validator_;
	ReconRuntime* recon_runtime_ = nullptr;

 public:
	explicit ToolRegistry(std::shared_ptr<spdlog::logger> logger);

	// Register built-in tools (compatible with OpenClaw)
	void RegisterBuiltinTools();

	// Register an external tool (from MCP server)
	void RegisterExternalTool(
			const std::string& name, const std::string& description,
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

	// Set cron scheduler and register cron agent tool
	void SetCronScheduler(std::shared_ptr<CronScheduler> sched);

	// Set session manager and register session agent tools
	void SetSessionManager(std::shared_ptr<SessionManager> mgr);

	// Set workspace root used for file-tool path validation
	void SetWorkspace(const std::string& path);

	// Set scope validator (recon mode: validates targets before execution)
	void SetScopeValidator(std::shared_ptr<ScopeValidator> validator);

	// Set recon runtime (for recording findings/probes in DuckDB)
	void SetReconRuntime(ReconRuntime* runtime);

	// Register recon tools (subdomain_enum, port_scan, etc.)
	void RegisterReconTools();

	// Execute a tool by name (with permission check)
	std::string ExecuteTool(const std::string& tool_name,
													const nlohmann::json& parameters);

	// Get tool schemas for LLM function calling (filtered by permissions)
	std::vector<ToolSchema> GetToolSchemas() const;

	// Check if tool is available
	bool HasTool(const std::string& tool_name) const;

	// Returns true if the tool call is classified as mutating and should be
	// gated by human approval when approval manager is enabled.
	bool IsMutatingToolCall(const std::string& tool_name,
													const nlohmann::json& parameters) const;

 private:
	// Permission check helper
	bool check_permission(const std::string& tool_name) const;

	// Helper to (re-)register a tool without duplicating its schema
	void register_tool(const std::string& name, const std::string& description,
										 nlohmann::json params_schema,
										 std::function<std::string(const nlohmann::json&)> handler);

	// Built-in tool implementations
	std::string read_file_tool(const nlohmann::json& params);
	std::string write_file_tool(const nlohmann::json& params);
	std::string edit_file_tool(const nlohmann::json& params);
	std::string exec_tool(const nlohmann::json& params);
	std::string message_tool(const nlohmann::json& params);
	std::string apply_patch_tool(const nlohmann::json& params);
	std::string process_tool(const nlohmann::json& params);
	std::string web_search_tool(const nlohmann::json& params);
	std::string web_fetch_tool(const nlohmann::json& params);
	std::string memory_search_tool(const nlohmann::json& params);
	std::string memory_get_tool(const nlohmann::json& params);

	bool should_request_mutation_approval(const std::string& tool_name,
																				const nlohmann::json& params) const;
	static bool looks_like_network_write(const nlohmann::json& params);
	static bool looks_like_mutating_action(const nlohmann::json& params);
	static std::string approval_summary(const std::string& tool_name,
																			const nlohmann::json& params);
};

}  // namespace quantclaw
