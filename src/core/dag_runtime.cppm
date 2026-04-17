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
  // Evolution events (emitted by EvolveRuntime).
  kEvolveRunStarted,
  kEvolveRoundStarted,
  kEvolveCandidateGenerated,
  kEvolveCandidateEvaluated,
  kEvolveLessonRecorded,
  kEvolveRunCompleted,
  kEvolveRunError,
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

  // Expose internals for ReconRuntime to share the same DuckDB connection.
  void* GetDatabase() const { return db_; }
  void* GetConnection() const { return con_; }
  std::mutex& GetMutex() { return db_mu_; }

 private:
  void init_schema();
  void prepare_statements();
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
  // Cached prepared statements — prepared once after schema init, reused per call.
  // Stored as void* to avoid exposing duckdb types in the module interface.
  void* stmt_insert_run_ = nullptr;
  void* stmt_insert_node_ = nullptr;
  void* stmt_insert_edge_ = nullptr;
  void* stmt_finalize_run_ = nullptr;
};

}  // namespace quantclaw