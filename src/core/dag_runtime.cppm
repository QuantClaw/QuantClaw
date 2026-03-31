// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.dag_runtime;

import std;
import nlohmann.json;
import quantclaw.common.noncopyable;

export namespace quantclaw {

enum class DagNodeType {
  kTurnStart,
  kContextAssembled,
  kMemoryManagement,
  kProviderResolved,
  kLlmResponse,
  kToolCall,
  kToolResult,
  kApprovalPending,
  kApprovalResolved,
  kCompaction,
  kTurnFinal,
  kTurnError,
  kTurnStopped,
};

std::string DagNodeTypeToString(DagNodeType type);

struct DagTurnState {
  std::string run_id;
  std::string last_node_id;
};

class DagRuntime : public Noncopyable {
 public:
  DagRuntime(const std::string& db_path, std::shared_ptr<spdlog::logger> logger);
  ~DagRuntime();

  bool IsEnabled() const {
    return enabled_;
  }

  DagTurnState BeginTurn(const std::string& session_key,
                         const std::string& user_message);

  std::string EmitNode(DagTurnState* turn_state, DagNodeType type,
                       const nlohmann::json& payload,
                       const std::string& edge_type = "next");

  void EndTurn(DagTurnState* turn_state, const std::string& status,
               const std::string& error = "");

  std::string LatestRunIdForSession(const std::string& session_key) const;

 private:
  void init_schema();
  std::string make_id(const std::string& prefix) const;
  std::string now_iso8601() const;

  void insert_run(const std::string& run_id, const std::string& session_key,
                  const std::string& user_message);
  void insert_node(const std::string& node_id, const std::string& run_id,
                   DagNodeType type, const nlohmann::json& payload);
  void insert_edge(const std::string& run_id, const std::string& from_node_id,
                   const std::string& to_node_id,
                   const std::string& edge_type);
  void finalize_run(const std::string& run_id, const std::string& status,
                    const std::string& error);

  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex db_mu_;
  void* db_ = nullptr;
  void* con_ = nullptr;
  bool enabled_ = false;
  std::unordered_map<std::string, std::string> latest_run_by_session_;
};

}  // namespace quantclaw