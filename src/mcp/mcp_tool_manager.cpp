// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/mcp/mcp_tool_manager.hpp"

#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw::mcp {

MCPToolManager::MCPToolManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  logger_->info("MCPToolManager initialized");
}

std::string MCPToolManager::MakeQualifiedName(const std::string& server_name,
                                              const std::string& tool_name) {
  return "mcp__" + server_name + "__" + tool_name;
}

void MCPToolManager::DiscoverTools(const MCPConfig& config) {
  for (const auto& server_cfg : config.servers) {
    if (server_cfg.name.empty() || server_cfg.url.empty()) {
      logger_->warn("Skipping MCP server with empty name or URL");
      continue;
    }

    logger_->info("Discovering tools from MCP server '{}' at {}",
                  server_cfg.name, server_cfg.url);

    try {
      auto client = std::make_shared<MCPClient>(server_cfg.url, logger_);
      auto tools = client->ListTools();

      clients_[server_cfg.name] = client;

      for (const auto& tool : tools) {
        std::string qualified = MakeQualifiedName(server_cfg.name, tool.name);
        tool_to_server_[qualified] = server_cfg.name;
        tool_to_original_name_[qualified] = tool.name;
        tool_meta_[qualified] = {tool.description, tool.parameters};

        logger_->info("Discovered MCP tool: {} -> {}", qualified,
                      tool.description);
      }

      logger_->info("Discovered {} tools from server '{}'", tools.size(),
                    server_cfg.name);
    } catch (const std::exception& e) {
      logger_->error("Failed to discover tools from '{}': {}", server_cfg.name,
                     e.what());
    }
  }

  logger_->info("Total MCP tools discovered: {}", tool_to_server_.size());
}

void MCPToolManager::RegisterInto(ToolRegistry& registry) {
  for (const auto& [qualified_name, meta] : tool_meta_) {
    auto self =
        this;  // capture raw pointer; MCPToolManager outlives the lambda
    const auto& name = qualified_name;  // C++17: structured bindings cannot be
                                        // captured in lambdas
    registry.RegisterExternalTool(
        qualified_name, meta.description, meta.parameters,
        [self, name](const nlohmann::json& args) -> std::string {
          return self->ExecuteTool(name, args);
        });
  }

  logger_->info("Registered {} MCP tools into ToolRegistry", tool_meta_.size());
}

std::string MCPToolManager::ExecuteTool(const std::string& qualified_name,
                                        const nlohmann::json& arguments) {
  auto server_it = tool_to_server_.find(qualified_name);
  if (server_it == tool_to_server_.end()) {
    throw std::runtime_error("Unknown MCP tool: " + qualified_name);
  }

  auto original_it = tool_to_original_name_.find(qualified_name);
  if (original_it == tool_to_original_name_.end()) {
    throw std::runtime_error("Missing original name for MCP tool: " +
                             qualified_name);
  }

  auto client_it = clients_.find(server_it->second);
  if (client_it == clients_.end()) {
    throw std::runtime_error("MCP client not found for server: " +
                             server_it->second);
  }

  logger_->debug("Executing MCP tool '{}' (server: '{}', original: '{}')",
                 qualified_name, server_it->second, original_it->second);

  auto response = client_it->second->CallTool(original_it->second, arguments);

  if (!response.error.empty()) {
    throw std::runtime_error("MCP tool error: " + response.error);
  }

  return response.result.dump();
}

bool MCPToolManager::IsExternalTool(const std::string& name) const {
  return tool_to_server_.count(name) > 0;
}

std::string
MCPToolManager::GetServerName(const std::string& qualified_name) const {
  auto it = tool_to_server_.find(qualified_name);
  if (it != tool_to_server_.end()) {
    return it->second;
  }
  return "";
}

std::string
MCPToolManager::GetOriginalToolName(const std::string& qualified_name) const {
  auto it = tool_to_original_name_.find(qualified_name);
  if (it != tool_to_original_name_.end()) {
    return it->second;
  }
  return "";
}

}  // namespace quantclaw::mcp
