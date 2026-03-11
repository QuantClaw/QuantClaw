// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/mcp/mcp_server.hpp"

#include <filesystem>
#include <sstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::mcp {

MCPTool::MCPTool(const std::string& name, const std::string& description)
    : name_(name), description_(description) {}

void MCPTool::AddParameter(const std::string& name, const std::string& type,
                           const std::string& description, bool required) {
  Parameter param;
  param.name = name;
  param.type = type;
  param.description = description;
  param.required = required;
  parameters_.push_back(param);
}

nlohmann::json MCPTool::GetSchema() const {
  nlohmann::json schema;
  schema["name"] = name_;
  schema["description"] = description_;

  nlohmann::json params;
  params["type"] = "object";
  params["properties"] = nlohmann::json::object();
  params["required"] = nlohmann::json::array();

  for (const auto& param : parameters_) {
    nlohmann::json prop;
    prop["type"] = param.type;
    prop["description"] = param.description;
    params["properties"][param.name] = prop;

    if (param.required) {
      params["required"].push_back(param.name);
    }
  }

  schema["inputSchema"] = params;
  return schema;
}

std::string MCPTool::Call(const nlohmann::json& arguments) {
  return execute(arguments);
}

MCPServer::MCPServer(std::shared_ptr<spdlog::logger> logger) : logger_(logger) {
  logger_->info("MCPServer initialized");
}

void MCPServer::RegisterTool(std::unique_ptr<MCPTool> tool) {
  auto name = tool->GetName();
  tools_[name] = std::move(tool);
  logger_->debug("Registered MCP tool: {}", name);
}

void MCPServer::RegisterResource(MCPResource resource) {
  auto uri = resource.uri;
  resources_[uri] = std::move(resource);
  logger_->debug("Registered MCP resource: {}", uri);
}

void MCPServer::RegisterPrompt(MCPPrompt prompt) {
  auto name = prompt.name;
  prompts_[name] = std::move(prompt);
  logger_->debug("Registered MCP prompt: {}", name);
}

nlohmann::json MCPServer::HandleRequest(const nlohmann::json& request) {
  try {
    std::string method = request.value("method", "");
    nlohmann::json id = request.value("id", nlohmann::json(nullptr));

    if (method == "initialize") {
      return handle_initialize(request, id);
    } else if (method == "tools/list" || method == "list_tools") {
      return handle_list_tools(request, id);
    } else if (method == "tools/call" || method == "call_tool") {
      return handle_call_tool(request, id);
    } else if (method == "resources/list") {
      return handle_list_resources(request, id);
    } else if (method == "resources/read") {
      return handle_read_resource(request, id);
    } else if (method == "prompts/list") {
      return handle_list_prompts(request, id);
    } else if (method == "prompts/get") {
      return handle_get_prompt(request, id);
    } else {
      logger_->warn("Unknown MCP method: {}", method);
      return create_error_response(id, -32601, "Method not found");
    }
  } catch (const std::exception& e) {
    logger_->error("Error handling MCP request: {}", e.what());
    return create_error_response(nlohmann::json(nullptr), -32603,
                                 "Internal error");
  }
}

nlohmann::json MCPServer::handle_initialize(const nlohmann::json& /*request*/,
                                            const nlohmann::json& id) {
  nlohmann::json result;
  result["protocolVersion"] = "2024-11-05";
  result["capabilities"] = {{"tools", nlohmann::json::object()},
                            {"resources", nlohmann::json::object()},
                            {"prompts", nlohmann::json::object()}};
  result["serverInfo"] = {{"name", "quantclaw"}, {"version", "0.3.0"}};
  return create_success_response(id, result);
}

nlohmann::json MCPServer::handle_list_tools(const nlohmann::json& /*request*/,
                                            const nlohmann::json& id) {
  nlohmann::json tools_array = nlohmann::json::array();
  for (const auto& [name, tool] : tools_) {
    tools_array.push_back(tool->GetSchema());
  }

  nlohmann::json result;
  result["tools"] = tools_array;

  return create_success_response(id, result);
}

nlohmann::json MCPServer::handle_call_tool(const nlohmann::json& request,
                                           const nlohmann::json& id) {
  try {
    std::string tool_name = request["params"]["name"];
    nlohmann::json arguments = request["params"]["arguments"];

    if (tools_.find(tool_name) == tools_.end()) {
      return create_error_response(id, -32602, "Tool not found: " + tool_name);
    }

    std::string result = tools_[tool_name]->Call(arguments);

    nlohmann::json response;
    response["content"] = {{{"type", "text"}, {"text", result}}};

    return create_success_response(id, response);
  } catch (const std::exception& e) {
    return create_error_response(
        id, -32603, "Tool execution failed: " + std::string(e.what()));
  }
}

nlohmann::json
MCPServer::handle_list_resources(const nlohmann::json& /*request*/,
                                 const nlohmann::json& id) {
  nlohmann::json resources_array = nlohmann::json::array();
  for (const auto& [uri, res] : resources_) {
    nlohmann::json entry;
    entry["uri"] = res.uri;
    entry["name"] = res.name;
    if (!res.description.empty())
      entry["description"] = res.description;
    if (!res.mime_type.empty())
      entry["mimeType"] = res.mime_type;
    resources_array.push_back(entry);
  }
  return create_success_response(id, {{"resources", resources_array}});
}

nlohmann::json MCPServer::handle_read_resource(const nlohmann::json& request,
                                               const nlohmann::json& id) {
  if (!request.contains("params") || !request["params"].is_object() ||
      !request["params"].contains("uri") ||
      !request["params"]["uri"].is_string()) {
    return create_error_response(id, -32602, "Missing required param: uri");
  }
  try {
    std::string uri = request["params"]["uri"];
    auto it = resources_.find(uri);
    if (it == resources_.end()) {
      return create_error_response(id, -32602, "Resource not found: " + uri);
    }
    std::string content = it->second.reader ? it->second.reader() : "";
    nlohmann::json result;
    result["contents"] = {
        {{"uri", uri},
         {"mimeType",
          it->second.mime_type.empty() ? "text/plain" : it->second.mime_type},
         {"text", content}}};
    return create_success_response(id, result);
  } catch (const std::exception& e) {
    return create_error_response(
        id, -32603, "Resource read failed: " + std::string(e.what()));
  }
}

nlohmann::json MCPServer::handle_list_prompts(const nlohmann::json& /*request*/,
                                              const nlohmann::json& id) {
  nlohmann::json prompts_array = nlohmann::json::array();
  for (const auto& [name, prompt] : prompts_) {
    nlohmann::json entry;
    entry["name"] = prompt.name;
    if (!prompt.description.empty())
      entry["description"] = prompt.description;
    nlohmann::json args_array = nlohmann::json::array();
    for (const auto& arg : prompt.arguments) {
      args_array.push_back({{"name", arg.name},
                            {"description", arg.description},
                            {"required", arg.required}});
    }
    entry["arguments"] = args_array;
    prompts_array.push_back(entry);
  }
  return create_success_response(id, {{"prompts", prompts_array}});
}

nlohmann::json MCPServer::handle_get_prompt(const nlohmann::json& request,
                                            const nlohmann::json& id) {
  if (!request.contains("params") || !request["params"].is_object() ||
      !request["params"].contains("name") ||
      !request["params"]["name"].is_string()) {
    return create_error_response(id, -32602, "Missing required param: name");
  }
  try {
    std::string name = request["params"]["name"];
    auto it = prompts_.find(name);
    if (it == prompts_.end()) {
      return create_error_response(id, -32602, "Prompt not found: " + name);
    }
    nlohmann::json args =
        request["params"].value("arguments", nlohmann::json::object());
    nlohmann::json messages = it->second.renderer ? it->second.renderer(args)
                                                  : nlohmann::json::array();
    return create_success_response(
        id, {{"description", it->second.description}, {"messages", messages}});
  } catch (const std::exception& e) {
    return create_error_response(
        id, -32603, "Prompt render failed: " + std::string(e.what()));
  }
}

nlohmann::json
MCPServer::create_success_response(const nlohmann::json& id,
                                   const nlohmann::json& result) {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = result;
  return response;
}

nlohmann::json MCPServer::create_error_response(const nlohmann::json& id,
                                                int code,
                                                const std::string& message) {
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = {{"code", code}, {"message", message}};
  return response;
}

}  // namespace quantclaw::mcp