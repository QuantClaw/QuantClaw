// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.mcp.mcp_tool_manager;

export import quantclaw.tools.tool_registry;

import quantclaw.mcp.mcp_client;

import std;

namespace spdlog {
class logger;
}

export namespace quantclaw {

struct MCPConfig;

namespace mcp {

class MCPToolManager {
 public:
  explicit MCPToolManager(std::shared_ptr<spdlog::logger> logger);

  // Connect to all configured MCP servers and discover their tools
  void DiscoverTools(const MCPConfig& config);

  // Register discovered tools into a ToolRegistry
  void RegisterInto(ToolRegistry& registry);

  // Execute an external tool by its qualified name
  std::string ExecuteTool(const std::string& qualified_name,
                          const std::string& arguments_json);

  // Name resolution helpers
  bool IsExternalTool(const std::string& name) const;
  std::string GetServerName(const std::string& qualified_name) const;
  std::string GetOriginalToolName(const std::string& qualified_name) const;

  // Get count of discovered tools
  size_t ToolCount() const {
    return tool_to_server_.size();
  }

  // Build qualified name: mcp__{server}__{tool}
  static std::string MakeQualifiedName(const std::string& server_name,
                                       const std::string& tool_name);

 private:
  std::shared_ptr<spdlog::logger> logger_;

  // server_name -> MCPClient
  std::unordered_map<std::string, std::shared_ptr<MCPClient>> clients_;

  // qualified_name -> server_name
  std::unordered_map<std::string, std::string> tool_to_server_;

  // qualified_name -> original tool name on the MCP server
  std::unordered_map<std::string, std::string> tool_to_original_name_;

  // qualified_name -> tool metadata (description, parameters)
  struct ToolMeta {
    std::string description;
    std::string parameters_json;
  };
  std::unordered_map<std::string, ToolMeta> tool_meta_;
};

}  // namespace mcp
}  // namespace quantclaw