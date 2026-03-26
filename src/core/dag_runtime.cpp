// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module quantclaw.core.dag_runtime;

import std;

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <sqlite3.h>

namespace quantclaw {

namespace {

bool exec_sql(sqlite3* db, const char* sql, std::string* error_out) {
  char* err = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (error_out) {
      *error_out = err ? err : "sqlite error";
    }
    if (err) {
      sqlite3_free(err);
    }
    return false;
  }
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
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(db_path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                               SQLITE_OPEN_FULLMUTEX,
                           nullptr);
  if (rc != SQLITE_OK) {
    if (logger_) {
      logger_->error("Failed to open DAG sqlite db '{}': {}", db_path,
                     sqlite3_errmsg(db));
    }
    if (db) {
      sqlite3_close(db);
    }
    return;
  }
  db_ = db;
  init_schema();
  enabled_ = db_ != nullptr;
  if (enabled_ && logger_) {
    logger_->info("DAG runtime enabled with sqlite db: {}", db_path);
  }
}

DagRuntime::~DagRuntime() {
  std::lock_guard<std::mutex> lock(db_mu_);
  if (db_) {
    sqlite3_close(static_cast<sqlite3*>(db_));
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
  auto* db = static_cast<sqlite3*>(db_);
  if (!db) {
    return;
  }

  std::string error;
  const char* kSchemaSql = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS dag_runs (
  run_id TEXT PRIMARY KEY,
  session_key TEXT NOT NULL,
  status TEXT NOT NULL,
  user_message TEXT,
  created_at TEXT NOT NULL,
  finished_at TEXT,
  error TEXT
);

CREATE TABLE IF NOT EXISTS dag_nodes (
  node_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  node_type TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  created_at TEXT NOT NULL,
  FOREIGN KEY(run_id) REFERENCES dag_runs(run_id)
);

CREATE TABLE IF NOT EXISTS dag_edges (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id TEXT NOT NULL,
  from_node_id TEXT NOT NULL,
  to_node_id TEXT NOT NULL,
  edge_type TEXT NOT NULL,
  created_at TEXT NOT NULL,
  FOREIGN KEY(run_id) REFERENCES dag_runs(run_id)
);

CREATE INDEX IF NOT EXISTS idx_dag_nodes_run_id ON dag_nodes(run_id);
CREATE INDEX IF NOT EXISTS idx_dag_edges_run_id ON dag_edges(run_id);
)SQL";

  if (!exec_sql(db, kSchemaSql, &error)) {
    if (logger_) {
      logger_->error("Failed to initialize DAG schema: {}", error);
    }
    sqlite3_close(db);
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
  auto* db = static_cast<sqlite3*>(db_);
  if (!db) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_runs(run_id, session_key, status, user_message, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (logger_) {
      logger_->error("DAG insert_run prepare failed: {}", sqlite3_errmsg(db));
    }
    return;
  }

  sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, session_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, "running", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, user_message.c_str(), -1, SQLITE_TRANSIENT);
  auto now = now_iso8601();
  sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE && logger_) {
    logger_->error("DAG insert_run step failed: {}", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

void DagRuntime::insert_node(const std::string& node_id,
                             const std::string& run_id, DagNodeType type,
                             const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto* db = static_cast<sqlite3*>(db_);
  if (!db) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_nodes(node_id, run_id, node_type, payload_json, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (logger_) {
      logger_->error("DAG insert_node prepare failed: {}", sqlite3_errmsg(db));
    }
    return;
  }

  auto payload_text = payload.dump();
  auto now = now_iso8601();
  auto type_text = DagNodeTypeToString(type);

  sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, run_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, type_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, payload_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE && logger_) {
    logger_->error("DAG insert_node step failed: {}", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

void DagRuntime::insert_edge(const std::string& run_id,
                             const std::string& from_node_id,
                             const std::string& to_node_id,
                             const std::string& edge_type) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto* db = static_cast<sqlite3*>(db_);
  if (!db) {
    return;
  }

  const char* sql =
      "INSERT INTO dag_edges(run_id, from_node_id, to_node_id, edge_type, "
      "created_at) VALUES(?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (logger_) {
      logger_->error("DAG insert_edge prepare failed: {}", sqlite3_errmsg(db));
    }
    return;
  }

  auto now = now_iso8601();
  sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, from_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, to_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, edge_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE && logger_) {
    logger_->error("DAG insert_edge step failed: {}", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

void DagRuntime::finalize_run(const std::string& run_id,
                              const std::string& status,
                              const std::string& error) {
  std::lock_guard<std::mutex> lock(db_mu_);
  auto* db = static_cast<sqlite3*>(db_);
  if (!db) {
    return;
  }

  const char* sql =
      "UPDATE dag_runs SET status=?, finished_at=?, error=? WHERE run_id=?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (logger_) {
      logger_->error("DAG finalize_run prepare failed: {}", sqlite3_errmsg(db));
    }
    return;
  }

  auto now = now_iso8601();
  sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, error.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, run_id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE && logger_) {
    logger_->error("DAG finalize_run step failed: {}", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

}  // namespace quantclaw
