// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_ui_compat.hpp"

#include "quantclaw/config.hpp"

#include <fstream>
#include <filesystem>

namespace quantclaw::gateway {

UiCompatHandler::UiCompatHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void UiCompatHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- agent.identity.get ---
  ctx_.server->RegisterHandler("agent.identity.get",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleAgentIdentityGet(params, client);
      });

  // --- node.list ---
  ctx_.server->RegisterHandler("node.list",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleNodeList(params, client);
      });

  // --- device.pair.list ---
  ctx_.server->RegisterHandler("device.pair.list",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleDevicePairList(params, client);
      });

  // --- logs.tail ---
  ctx_.server->RegisterHandler("logs.tail",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleLogsTail(params, client);
      });

  // --- config.schema ---
  ctx_.server->RegisterHandler("config.schema",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleConfigSchema(params, client);
      });

  // --- sessions.usage ---
  ctx_.server->RegisterHandler("sessions.usage",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSessionsUsage(params, client);
      });

  // --- usage.cost ---
  ctx_.server->RegisterHandler("usage.cost",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleUsageCost(params, client);
      });

  // --- sessions.usage.timeseries ---
  ctx_.server->RegisterHandler("sessions.usage.timeseries",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSessionsUsageTimeseries(params, client);
      });

  // --- sessions.usage.logs ---
  ctx_.server->RegisterHandler("sessions.usage.logs",
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleSessionsUsageLogs(params, client);
      });
}

nlohmann::json UiCompatHandler::HandleAgentIdentityGet(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"agentId", "main"},
          {"name", "QuantClaw Agent"},
          {"avatar", ""},
          {"emoji", "\xF0\x9F\xA6\x9E"}};
}

nlohmann::json UiCompatHandler::HandleNodeList(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return nlohmann::json::array();
}

nlohmann::json UiCompatHandler::HandleDevicePairList(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return nlohmann::json::array();
}

nlohmann::json UiCompatHandler::HandleLogsTail(const nlohmann::json& params, ClientConnection& /*client*/) {
  int req_limit = params.value("limit", 200);
  int max_bytes = params.value("maxBytes", 512 * 1024);
  long long cursor = params.value("cursor", 0LL);
  (void)max_bytes;
  (void)cursor;

  nlohmann::json lines = nlohmann::json::array();
  long long new_cursor = cursor;
  bool truncated = false;

  namespace fs = std::filesystem;
  // Log path is derived from config directory: <config_dir>/logs/gateway.log
  std::string config_path = ctx_.config ? 
      quantclaw::QuantClawConfig::DefaultConfigPath() : "/tmp/quantclaw/config.json";
  std::string log_file_path = 
      (fs::path(config_path).parent_path() / "logs" / "gateway.log").string();
  fs::path safe = fs::path(log_file_path).lexically_normal();
  bool path_ok = !safe.empty() && safe.filename() == "gateway.log" &&
                 safe.parent_path().filename() == "logs" &&
                 fs::exists(safe);
  if (path_ok) {
    std::ifstream ifs(safe);
    if (ifs.is_open()) {
      std::vector<std::string> all_lines;
      std::string line;
      while (std::getline(ifs, line)) {
        all_lines.push_back(line);
      }
      int total = static_cast<int>(all_lines.size());
      int start = std::max(0, total - req_limit);
      for (size_t i = static_cast<size_t>(start); i < static_cast<size_t>(total); ++i) {
        lines.push_back(all_lines[i]);
      }
      new_cursor = total;
      truncated = start > 0;
    }
  }

  return {{"file", log_file_path},
          {"cursor", new_cursor},
          {"lines", lines},
          {"truncated", truncated}};
}

nlohmann::json UiCompatHandler::HandleConfigSchema(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  nlohmann::json schema = nlohmann::json::parse(R"JSON({
    "type": "object",
    "properties": {
      "agent": {
        "type": "object",
        "properties": {
          "model": {"type": "string"},
          "maxIterations": {"type": "integer", "minimum": 1, "maximum": 500},
          "temperature": {"type": "number", "minimum": 0, "maximum": 2},
          "maxTokens": {"type": "integer", "minimum": 1},
          "thinking": {
            "type": "string",
            "enum": ["off", "low", "medium", "high"]
          }
        }
      },
      "gateway": {
        "type": "object",
        "properties": {
          "port": {"type": "integer", "minimum": 1, "maximum": 65535},
          "bind": {"type": "string"}
        }
      }
    }
  })JSON");
  nlohmann::json ui_hints = nlohmann::json::parse(R"JSON({
    "agent.model": {"label": "Model", "group": "Agent"},
    "agent.maxIterations": {"label": "Max Iterations", "group": "Agent"},
    "agent.temperature": {"label": "Temperature", "group": "Agent", "advanced": true},
    "agent.thinking": {"label": "Thinking Mode", "group": "Agent"},
    "gateway.port": {"label": "Port", "group": "Gateway"},
    "gateway.bind": {"label": "Bind Address", "group": "Gateway"}
  })JSON");
  return {{"schema", schema},
          {"uiHints", ui_hints},
          {"version", "1"},
          {"generatedAt", ""}};
}

nlohmann::json UiCompatHandler::HandleSessionsUsage(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string start_date = params.value("startDate", "");
  std::string end_date = params.value("endDate", "");

  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  long long total_input = 0, total_output = 0;
  nlohmann::json session_entries = nlohmann::json::array();

  nlohmann::json zero_totals = {
      {"input", total_input},
      {"output", total_output},
      {"cacheRead", 0},
      {"cacheWrite", 0},
      {"totalTokens", total_input + total_output},
      {"totalCost", 0.0},
      {"inputCost", 0.0},
      {"outputCost", 0.0},
      {"cacheReadCost", 0.0},
      {"cacheWriteCost", 0.0},
      {"missingCostEntries", 0}};

  nlohmann::json result;
  result["updatedAt"] = now_ms;
  result["startDate"] = start_date;
  result["endDate"] = end_date;
  result["sessions"] = session_entries;
  result["totals"] = zero_totals;
  result["aggregates"] = nlohmann::json::parse(R"JSON({
    "messages": {
      "total": 0,
      "user": 0,
      "assistant": 0,
      "toolCalls": 0,
      "toolResults": 0,
      "errors": 0
    },
    "tools": {
      "totalCalls": 0,
      "uniqueTools": 0,
      "tools": []
    },
    "byModel": [],
    "byProvider": [],
    "byAgent": [],
    "byChannel": [],
    "daily": []
  })JSON");
  
  return result;
}

nlohmann::json UiCompatHandler::HandleUsageCost(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string start_date = params.value("startDate", "");
  std::string end_date = params.value("endDate", "");
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  nlohmann::json zero_totals = {{"input", 0},
                                {"output", 0},
                                {"cacheRead", 0},
                                {"cacheWrite", 0},
                                {"totalTokens", 0},
                                {"totalCost", 0.0},
                                {"inputCost", 0.0},
                                {"outputCost", 0.0},
                                {"cacheReadCost", 0.0},
                                {"cacheWriteCost", 0.0},
                                {"missingCostEntries", 0}};
  return {{"updatedAt", now_ms},
          {"days", 0},
          {"daily", nlohmann::json::array()},
          {"totals", zero_totals}};
}

nlohmann::json UiCompatHandler::HandleSessionsUsageTimeseries(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"sessionId", nullptr},
          {"points", nlohmann::json::array()}};
}

nlohmann::json UiCompatHandler::HandleSessionsUsageLogs(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"logs", nlohmann::json::array()}};
}

}  // namespace quantclaw::gateway
