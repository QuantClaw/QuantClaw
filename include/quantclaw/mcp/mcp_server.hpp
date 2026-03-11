// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::mcp {

class MCPTool {
 public:
  struct Parameter {
    std::string name;
    std::string type;
    std::string description;
    bool required = false;
  };

  MCPTool(const std::string& name, const std::string& description);

  virtual ~MCPTool() = default;
  const std::string& GetName() const {
    return name_;
  }
  const std::string& GetDescription() const {
    return description_;
  }

  void AddParameter(const std::string& name, const std::string& type,
                    const std::string& description, bool required = false);

  nlohmann::json GetSchema() const;
  std::string Call(const nlohmann::json& arguments);

 protected:
  virtual std::string execute(const nlohmann::json& arguments) = 0;

 private:
  std::string name_;
  std::string description_;
  std::vector<Parameter> parameters_;
};

// MCP Resource (read-only data source exposed to the LLM)
struct MCPResource {
  std::string uri;   // Unique resource URI (e.g. "file:///path/to/file")
  std::string name;  // Human-readable name
  std::string description;
  std::string mime_type;  // e.g. "text/plain", "application/json"

  // Returns text content of the resource
  std::function<std::string()> reader;
};

// MCP Prompt (reusable prompt template)
struct MCPPrompt {
  struct Argument {
    std::string name;
    std::string description;
    bool required = false;
  };

  std::string name;
  std::string description;
  std::vector<Argument> arguments;

  // Given named argument values, returns rendered messages array
  std::function<nlohmann::json(const nlohmann::json& args)> renderer;
};

class MCPServer {
 public:
  explicit MCPServer(std::shared_ptr<spdlog::logger> logger);

  void RegisterTool(std::unique_ptr<MCPTool> tool);
  void RegisterResource(MCPResource resource);
  void RegisterPrompt(MCPPrompt prompt);
  nlohmann::json HandleRequest(const nlohmann::json& request);

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::unordered_map<std::string, std::unique_ptr<MCPTool>> tools_;
  std::unordered_map<std::string, MCPResource> resources_;
  std::unordered_map<std::string, MCPPrompt> prompts_;

  nlohmann::json handle_initialize(const nlohmann::json& request,
                                   const nlohmann::json& id);
  nlohmann::json handle_list_tools(const nlohmann::json& request,
                                   const nlohmann::json& id);
  nlohmann::json handle_call_tool(const nlohmann::json& request,
                                  const nlohmann::json& id);
  nlohmann::json handle_list_resources(const nlohmann::json& request,
                                       const nlohmann::json& id);
  nlohmann::json handle_read_resource(const nlohmann::json& request,
                                      const nlohmann::json& id);
  nlohmann::json handle_list_prompts(const nlohmann::json& request,
                                     const nlohmann::json& id);
  nlohmann::json handle_get_prompt(const nlohmann::json& request,
                                   const nlohmann::json& id);

  nlohmann::json create_success_response(const nlohmann::json& id,
                                         const nlohmann::json& result);
  nlohmann::json create_error_response(const nlohmann::json& id, int code,
                                       const std::string& message);
};

}  // namespace quantclaw::mcp