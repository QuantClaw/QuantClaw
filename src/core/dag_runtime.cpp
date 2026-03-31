// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>
#include <duckdb.h>

module quantclaw.core.dag_runtime;

import std;
import nlohmann.json;
import quantclaw.common.noncopyable;

namespace quantclaw {

namespace {

bool exec_sql(duckdb_connection con, const char* sql, std::string* error_out) {
  duckdb_result result;
  if (duckdb_query(con, sql, &result) == DuckDBError) {
    if (error_out) {
      const char* err = duckdb_result_error(&result);
      *error_out = err ? err : "duckdb error";
    }
    duckdb_destroy_result(&result);
    return false;
  }
  duckdb_destroy_result(&result);
  return true;
}

}  // namespace

std::string DagNodeTypeToString(DagNodeType type) {
  switch (type) {
    case DagNodeType::kTurnStart:
      return "turn_start";
    case DagNodeType::kContextAssembled:
      return "context_assembled";
    case DagNodeType::kMemoryManagement:
      return "memory_management";
    case DagNodeType::kProviderResolved:
      return "provider_resolved";
    case DagNodeType::kLlmResponse:
      return "llm_response";
    case DagNodeType::kToolCall:
      return "tool_call";
    case DagNodeType::kToolResult:
      return "tool_result";
    case DagNodeType::kApprovalPending:
      return "approval_pending";
    case DagNodeType::kApprovalResolved:
      return "approval_resolved";
    case DagNodeType::kCompaction:
      return "compaction";
    case DagNodeType::kTurnFinal:
      return "turn_final";
    case DagNodeType::kTurnError:
      return "turn_error";
    case DagNodeType::kTurnStopped:
      return "turn_stopped";
  }
  return "unknown";
}

DagRuntime::DagRuntime(const std::string& db_path,
                       std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  duckdb_database db = nullptr;
  if (duckdb_open(db_path.c_str(), &db) == DuckDBError) {
    if (logger_) {
      logger_->error("Failed to open DAG DuckDB db '{}': {}", db_path,
                     "open failed");
    }
    if (db) {
      duckdb_close(&db);
    }
    return;
  }

  duckdb_connection con = nullptr;
  if (duckdb_connect(db, &con) == DuckDBError) {
    if (logger_) {
      logger_->error("Failed to connect to DAG DuckDB db '{}': {}", db_path,
                     "connect failed");
    }
    duckdb_close(&db);
    return;
  }

  db_ = db;
  con_ = con;
  init_schema();
  enabled_ = db_ != nullptr && con_ != nullptr;
  if (enabled_ && logger_) {
    logger_->info("DAG runtime enabled with DuckDB db: {}", db_path);
  }
}

DagRuntime::~DagRuntime() {
  std::lock_guard<std::mutex> lock(db_mu_);
  if (con_) {
    auto con = static_cast<duckdb_connection>(con_);
    duckdb_disconnect(&con);
    con_ = nullptr;
  }
  if (db_) {
    auto db = static_cast<duckdb_database>(db_);
    duckdb_close(&db);
    db_ = nullptr;
  }
}

DagTurnState DagRuntime::BeginTurn(const std::string& session_key,
                                   const std::string& user_message) {
  DagTurnState state;
  if (!enabled_) {
    return state;
  }

  state.run_id = make_id("run");
  insert_run(state.run_id, session_key, user_message);
  {
    std::lock_guard<std::mutex> lock(db_mu_);
    latest_run_by_session_[session_key] = state.run_id;
  }
  state.last_node_id =
      EmitNode(&state, DagNodeType::kTurnStart,
               nlohmann::json{{"sessionKey", session_key}});
  return state;
}

std::string DagRuntime::EmitNode(DagTurnState* turn_state, DagNodeType type,
                                 const nlohmann::json& payload,
                                 const std::string& edge_type) {
  if (!enabled_ || !turn_state || turn_state->run_id.empty()) {
    return "";
  }

  const std::string node_id = make_id("node");
  insert_node(node_id, turn_state->run_id, type, payload);

  if (!turn_state->last_node_id.empty()) {
    insert_edge(turn_state->run_id, turn_state->last_node_id, node_id,
                edge_type);
  }

  turn_state->last_node_id = node_id;
  return node_id;
}

void DagRuntime::EndTurn(DagTurnState* turn_state, const std::string& status,
                         const std::string& error) {
  if (!enabled_ || !turn_state || turn_state->run_id.empty()) {
    return;
  }
  finalize_run(turn_state->run_id, status, error);
}

std::string DagRuntime::LatestRunIdForSession(
    const std::string& session_key) const {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto it = latest_run_by_session_.find(session_key);
  if (it == latest_run_by_session_.end()) {
    return "";
  }
  return it->second;
}

void DagRuntime::init_schema() {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto con = static_cast<duckdb_connection>(con_);
  auto db = static_cast<duckdb_database>(db_);
  if (!db || !con) {
    return;
  }

  std::string error;
  const char* kSchemaSql[] = {
      R"SQL(
CREATE TABLE IF NOT EXISTS dag_runs (
  run_id TEXT PRIMARY KEY,
  session_key TEXT NOT NULL,
  status TEXT NOT NULL,
  user_message TEXT,
  created_at TEXT NOT NULL,
  finished_at TEXT,
  error TEXT
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS dag_nodes (
  node_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  node_type TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  created_at TEXT NOT NULL
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS dag_edges (
  run_id TEXT NOT NULL,
  from_node_id TEXT NOT NULL,
  to_node_id TEXT NOT NULL,
  edge_type TEXT NOT NULL,
  created_at TEXT NOT NULL
);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_dag_nodes_run_id ON dag_nodes(run_id);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_dag_edges_run_id ON dag_edges(run_id);
)SQL",
  };

  for (const auto* stmt : kSchemaSql) {
    if (!exec_sql(con, stmt, &error)) {
      break;
    }
  }

  if (!error.empty()) {
    if (logger_) {
      logger_->error("Failed to initialize DAG schema: {}", error);
    }
    auto con_handle = con;
    auto db_handle = db;
    duckdb_disconnect(&con_handle);
    duckdb_close(&db_handle);
    con_ = nullptr;
    db_ = nullptr;
  }
}

std::string DagRuntime::make_id(const std::string& prefix) const {
  thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist;

  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::ostringstream oss;
  oss << prefix << "_" << ms << "_" << std::hex << dist(rng);
  return oss.str();
}

std::string DagRuntime::now_iso8601() const {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

void DagRuntime::insert_run(const std::string& run_id,
                            const std::string& session_key,
                            const std::string& user_message) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto con = static_cast<duckdb_connection>(con_);
  if (!con) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_runs(run_id, session_key, status, user_message, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  duckdb_prepared_statement stmt;
  if (duckdb_prepare(con, sql, &stmt) == DuckDBError) {
    if (logger_) {
      logger_->error("DAG insert_run prepare failed: {}",
                     duckdb_prepare_error(stmt));
    }
    duckdb_destroy_prepare(&stmt);
    return;
  }

  duckdb_bind_varchar(stmt, 1, run_id.c_str());
  duckdb_bind_varchar(stmt, 2, session_key.c_str());
  duckdb_bind_varchar(stmt, 3, "running");
  duckdb_bind_varchar(stmt, 4, user_message.c_str());
  auto now = now_iso8601();
  duckdb_bind_varchar(stmt, 5, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("DAG insert_run step failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
  duckdb_destroy_prepare(&stmt);
}

void DagRuntime::insert_node(const std::string& node_id,
                             const std::string& run_id, DagNodeType type,
                             const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto con = static_cast<duckdb_connection>(con_);
  if (!con) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_nodes(node_id, run_id, node_type, payload_json, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  duckdb_prepared_statement stmt;
  if (duckdb_prepare(con, sql, &stmt) == DuckDBError) {
    if (logger_) {
      logger_->error("DAG insert_node prepare failed: {}",
                     duckdb_prepare_error(stmt));
    }
    duckdb_destroy_prepare(&stmt);
    return;
  }

  auto payload_text = payload.dump();
  auto now = now_iso8601();
  auto type_text = DagNodeTypeToString(type);

  duckdb_bind_varchar(stmt, 1, node_id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, type_text.c_str());
  duckdb_bind_varchar(stmt, 4, payload_text.c_str());
  duckdb_bind_varchar(stmt, 5, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("DAG insert_node step failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
  duckdb_destroy_prepare(&stmt);
}

void DagRuntime::insert_edge(const std::string& run_id,
                             const std::string& from_node_id,
                             const std::string& to_node_id,
                             const std::string& edge_type) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto con = static_cast<duckdb_connection>(con_);
  if (!con) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_edges(run_id, from_node_id, to_node_id, edge_type, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  duckdb_prepared_statement stmt;
  if (duckdb_prepare(con, sql, &stmt) == DuckDBError) {
    if (logger_) {
      logger_->error("DAG insert_edge prepare failed: {}",
                     duckdb_prepare_error(stmt));
    }
    duckdb_destroy_prepare(&stmt);
    return;
  }

  auto now = now_iso8601();
  duckdb_bind_varchar(stmt, 1, run_id.c_str());
  duckdb_bind_varchar(stmt, 2, from_node_id.c_str());
  duckdb_bind_varchar(stmt, 3, to_node_id.c_str());
  duckdb_bind_varchar(stmt, 4, edge_type.c_str());
  duckdb_bind_varchar(stmt, 5, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("DAG insert_edge step failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
  duckdb_destroy_prepare(&stmt);
}

void DagRuntime::finalize_run(const std::string& run_id,
                              const std::string& status,
                              const std::string& error) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto con = static_cast<duckdb_connection>(con_);
  if (!con) {
    return;
  }

  const char* sql =
      "UPDATE dag_runs SET status=?, finished_at=?, error=? WHERE run_id=?;";
  duckdb_prepared_statement stmt;
  if (duckdb_prepare(con, sql, &stmt) == DuckDBError) {
    if (logger_) {
      logger_->error("DAG finalize_run prepare failed: {}",
                     duckdb_prepare_error(stmt));
    }
    duckdb_destroy_prepare(&stmt);
    return;
  }

  auto now = now_iso8601();
  duckdb_bind_varchar(stmt, 1, status.c_str());
  duckdb_bind_varchar(stmt, 2, now.c_str());
  duckdb_bind_varchar(stmt, 3, error.c_str());
  duckdb_bind_varchar(stmt, 4, run_id.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("DAG finalize_run step failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
  duckdb_destroy_prepare(&stmt);
}

}  // namespace quantclaw
