// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/mcp/mcp_client.hpp"

#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw::mcp {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                            std::string* userp) {
  userp->append((char*)contents, size * nmemb);
  return size * nmemb;
}

MCPClient::MCPClient(const std::string& server_url,
                     std::shared_ptr<spdlog::logger> logger)
    : server_url_(server_url), logger_(logger) {
  logger_->info("MCPClient initialized with server: {}", server_url_);
}

std::vector<Tool> MCPClient::ListTools() {
  nlohmann::json request;
  request["jsonrpc"] = "2.0";
  request["method"] = "tools/list";
  request["id"] = ++request_id_;

  nlohmann::json response = make_request(request);

  std::vector<Tool> tools;
  if (response.contains("result") && response["result"].contains("tools")) {
    for (const auto& tool_json : response["result"]["tools"]) {
      Tool tool;
      tool.name = tool_json.value("name", "");
      tool.description = tool_json.value("description", "");
      // Accept both MCP-spec "inputSchema" and legacy "parameters"
      if (tool_json.contains("inputSchema")) {
        tool.parameters = tool_json["inputSchema"];
      } else {
        tool.parameters =
            tool_json.value("parameters", nlohmann::json::object());
      }
      tools.push_back(tool);
    }
  }

  return tools;
}

MCPResponse MCPClient::CallTool(const std::string& tool_name,
                                const nlohmann::json& arguments) {
  nlohmann::json request;
  request["jsonrpc"] = "2.0";
  request["method"] = "tools/call";
  request["params"] = {{"name", tool_name}, {"arguments", arguments}};
  request["id"] = ++request_id_;

  try {
    nlohmann::json response = make_request(request);

    MCPResponse result;
    if (response.contains("result")) {
      result.result = response["result"];
    } else if (response.contains("error")) {
      result.error = response["error"].value("message", "Unknown error");
    }

    return result;
  } catch (const std::exception& e) {
    MCPResponse result;
    result.error = "Request failed: " + std::string(e.what());
    return result;
  }
}

nlohmann::json MCPClient::make_request(const nlohmann::json& request) {
  CURL* curl;
  CURLcode res;
  std::string read_buffer;

  curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize CURL");
  }

  // Set URL
  curl_easy_setopt(curl, CURLOPT_URL, server_url_.c_str());

  // Set POST data
  std::string json_payload = request.dump();
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());

  // Set headers
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  // Set write callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

  // Set timeout
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  // Perform request
  res = curl_easy_perform(curl);

  // Cleanup
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    throw std::runtime_error("CURL request failed: " +
                             std::string(curl_easy_strerror(res)));
  }

  return nlohmann::json::parse(read_buffer);
}

}  // namespace quantclaw::mcp