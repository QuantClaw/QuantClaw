// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/mcp/mcp_server.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <filesystem>

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
    
    schema["parameters"] = params;
    return schema;
}

std::string MCPTool::Call(const nlohmann::json& arguments) {
    return execute(arguments);
}

MCPServer::MCPServer(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
    logger_->info("MCPServer initialized");
}

void MCPServer::RegisterTool(std::unique_ptr<MCPTool> tool) {
    auto name = tool->GetName();
    tools_[name] = std::move(tool);
    logger_->debug("Registered MCP tool: {}", name);
}

nlohmann::json MCPServer::HandleRequest(const nlohmann::json& request) {
    try {
        std::string method = request.value("method", "");
        nlohmann::json id = request.value("id", nlohmann::json(nullptr));
        
        if (method == "initialize") {
            return handle_initialize(request, id);
        } else if (method == "list_tools") {
            return handle_list_tools(request, id);
        } else if (method == "call_tool") {
            return handle_call_tool(request, id);
        } else {
            logger_->warn("Unknown MCP method: {}", method);
            return create_error_response(id, -32601, "Method not found");
        }
    } catch (const std::exception& e) {
        logger_->error("Error handling MCP request: {}", e.what());
        return create_error_response(nlohmann::json(nullptr), -32603, "Internal error");
    }
}

nlohmann::json MCPServer::handle_initialize(const nlohmann::json& /*request*/, const nlohmann::json& id) {
    nlohmann::json result;
    result["protocol_version"] = "2024-11-15";
    result["capabilities"] = {
        {"tools", {}}
    };
    
    return create_success_response(id, result);
}

nlohmann::json MCPServer::handle_list_tools(const nlohmann::json& /*request*/, const nlohmann::json& id) {
    nlohmann::json tools_array = nlohmann::json::array();
    for (const auto& [name, tool] : tools_) {
        tools_array.push_back(tool->GetSchema());
    }
    
    nlohmann::json result;
    result["tools"] = tools_array;
    
    return create_success_response(id, result);
}

nlohmann::json MCPServer::handle_call_tool(const nlohmann::json& request, const nlohmann::json& id) {
    try {
        std::string tool_name = request["params"]["name"];
        nlohmann::json arguments = request["params"]["arguments"];
        
        if (tools_.find(tool_name) == tools_.end()) {
            return create_error_response(id, -32602, "Tool not found: " + tool_name);
        }
        
        std::string result = tools_[tool_name]->Call(arguments);
        
        nlohmann::json response;
        response["content"] = {{
            {"type", "text"},
            {"text", result}
        }};
        
        return create_success_response(id, response);
    } catch (const std::exception& e) {
        return create_error_response(id, -32603, "Tool execution failed: " + std::string(e.what()));
    }
}

nlohmann::json MCPServer::create_success_response(const nlohmann::json& id, const nlohmann::json& result) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = result;
    return response;
}

nlohmann::json MCPServer::create_error_response(const nlohmann::json& id, int code, const std::string& message) {
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["error"] = {
        {"code", code},
        {"message", message}
    };
    return response;
}

} // namespace quantclaw::mcp