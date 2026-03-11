// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include "quantclaw/core/memory_manager.hpp"
#include "quantclaw/mcp/mcp_server.hpp"
#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw::mcp {

class QuantClawMCPTools {
 private:
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::shared_ptr<spdlog::logger> logger_;

 public:
  QuantClawMCPTools(std::shared_ptr<MemoryManager> memory_manager,
                    std::shared_ptr<ToolRegistry> tool_registry,
                    std::shared_ptr<spdlog::logger> logger);

  void register_builtin_tools(MCPServer& server);

 private:
  // Built-in MCP tools
  class ReadFileTool : public MCPTool {
   public:
    ReadFileTool(std::shared_ptr<MemoryManager> memory_manager,
                 std::shared_ptr<spdlog::logger> logger);
    std::string execute(const nlohmann::json& arguments) override;

   private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<spdlog::logger> logger_;
  };

  class WriteFileTool : public MCPTool {
   public:
    WriteFileTool(std::shared_ptr<MemoryManager> memory_manager,
                  std::shared_ptr<spdlog::logger> logger);
    std::string execute(const nlohmann::json& arguments) override;

   private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<spdlog::logger> logger_;
  };

  class EditFileTool : public MCPTool {
   public:
    EditFileTool(std::shared_ptr<MemoryManager> memory_manager,
                 std::shared_ptr<spdlog::logger> logger);
    std::string execute(const nlohmann::json& arguments) override;

   private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<spdlog::logger> logger_;
  };

  class ExecTool : public MCPTool {
   public:
    ExecTool(std::shared_ptr<ToolRegistry> tool_registry,
             std::shared_ptr<spdlog::logger> logger);
    std::string execute(const nlohmann::json& arguments) override;

   private:
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::shared_ptr<spdlog::logger> logger_;
  };
};

}  // namespace quantclaw::mcp