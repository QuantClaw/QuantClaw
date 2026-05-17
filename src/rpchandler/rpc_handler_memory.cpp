// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_memory.hpp"

#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/platform/process.hpp"
#include <filesystem>

namespace quantclaw::gateway {

MemoryHandler::MemoryHandler(const HandlerContext& ctx)
    : ctx_(ctx) {
  workspace_ = (std::filesystem::path(quantclaw::platform::home_directory()) /
               ".quantclaw/agents/main/workspace")
                   .string();
}

void MemoryHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- memory.status ---
  ctx_.server->RegisterHandler(
      methods::kMemoryStatus,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleMemoryStatus(params, client);
      });

  // --- memory.search ---
  ctx_.server->RegisterHandler(
      methods::kMemorySearch,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleMemorySearch(params, client);
      });
}

nlohmann::json MemoryHandler::HandleMemoryStatus(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  quantclaw::MemorySearch search(ctx_.logger);
  search.IndexDirectory(workspace_);
  return search.Stats();
}

nlohmann::json MemoryHandler::HandleMemorySearch(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string query = params.value("query", "");
  int max_results = params.value("maxResults", 10);
  if (query.empty()) {
    throw std::runtime_error("query is required");
  }
  quantclaw::MemorySearch search(ctx_.logger);
  search.IndexDirectory(workspace_);
  auto results = search.Search(query, max_results);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : results) {
    arr.push_back({{"source", r.source},
                   {"content", r.content},
                   {"score", r.score},
                   {"lineNumber", r.line_number}});
  }
  return arr;
}

}  // namespace quantclaw::gateway
