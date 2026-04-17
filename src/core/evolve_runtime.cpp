// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>
#include <duckdb.h>
#include <ctime>

module quantclaw.core.evolve_runtime;

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;

namespace quantclaw {

namespace {

bool run_sql(duckdb_connection con, const char* sql, std::string* error_out) {
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

void bind_str_or_null(duckdb_prepared_statement stmt, idx_t idx,
                      const std::string& value) {
  if (value.empty()) {
    duckdb_bind_null(stmt, idx);
  } else {
    duckdb_bind_varchar(stmt, idx, value.c_str());
  }
}

}  // namespace

EvolveRuntime::EvolveRuntime(DagRuntime* dag_runtime,
                             std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)), dag_runtime_(dag_runtime) {
  if (!dag_runtime_ || !dag_runtime_->IsEnabled()) {
    logger_->warn("EvolveRuntime: DagRuntime not available, evolve graph "
                  "disabled");
    return;
  }

  init_schema();
  if (enabled_) {
    prepare_statements();
  }

  if (enabled_) {
    logger_->info(
        "EvolveRuntime enabled, sharing DagRuntime DuckDB connection");
  }
}

EvolveRuntime::~EvolveRuntime() {
  if (!dag_runtime_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto destroy = [](void*& raw) {
    if (raw) {
      auto s = static_cast<duckdb_prepared_statement>(raw);
      duckdb_destroy_prepare(&s);
      raw = nullptr;
    }
  };

  destroy(stmt_insert_run_);
  destroy(stmt_update_run_state_);
  destroy(stmt_update_run_state_err_);
  destroy(stmt_update_run_best_);
  destroy(stmt_insert_candidate_);
  destroy(stmt_update_candidate_eval_);
  destroy(stmt_insert_lesson_);
  destroy(stmt_insert_cognition_);
  destroy(stmt_insert_edge_);
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

void EvolveRuntime::init_schema() {
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  if (!con) return;

  std::string error;
  const char* kSchemaSql[] = {
      R"SQL(
CREATE TABLE IF NOT EXISTS evolve_runs (
  run_id TEXT PRIMARY KEY,
  session_key TEXT,
  experiment_name TEXT NOT NULL,
  run_spec_json TEXT NOT NULL,
  state TEXT NOT NULL,
  started_at TEXT NOT NULL,
  ended_at TEXT,
  best_score DOUBLE,
  best_candidate_id TEXT,
  wall_seconds DOUBLE,
  config_fingerprint TEXT,
  last_error TEXT
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS evolve_candidates (
  candidate_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  round INTEGER NOT NULL,
  parent_id TEXT,
  program_hash TEXT,
  program_path TEXT,
  motivation TEXT,
  score DOUBLE,
  metrics_json TEXT,
  state TEXT NOT NULL,
  eval_wall_seconds DOUBLE,
  created_at TEXT NOT NULL,
  llm_call_count INTEGER DEFAULT 0,
  total_tokens_in INTEGER DEFAULT 0,
  total_tokens_out INTEGER DEFAULT 0
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS evolve_lessons (
  lesson_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  round INTEGER NOT NULL,
  candidate_id TEXT,
  text TEXT NOT NULL,
  tags_json TEXT,
  created_at TEXT NOT NULL,
  embedding BLOB
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS evolve_cognition (
  cognition_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  source TEXT NOT NULL,
  title TEXT,
  content TEXT NOT NULL,
  tags_json TEXT,
  embedding BLOB,
  created_at TEXT NOT NULL
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS evolve_edges (
  edge_id TEXT PRIMARY KEY,
  run_id TEXT NOT NULL,
  from_id TEXT NOT NULL,
  to_id TEXT NOT NULL,
  edge_type TEXT NOT NULL,
  metadata_json TEXT,
  created_at TEXT NOT NULL
);
)SQL",
      "CREATE INDEX IF NOT EXISTS idx_evolve_candidates_run "
      "ON evolve_candidates(run_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_candidates_parent "
      "ON evolve_candidates(parent_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_lessons_run "
      "ON evolve_lessons(run_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_cognition_run "
      "ON evolve_cognition(run_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_edges_run "
      "ON evolve_edges(run_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_edges_from "
      "ON evolve_edges(from_id);",
      "CREATE INDEX IF NOT EXISTS idx_evolve_edges_to "
      "ON evolve_edges(to_id);",
  };

  for (const auto* stmt : kSchemaSql) {
    if (!run_sql(con, stmt, &error)) {
      logger_->error("EvolveRuntime schema init failed: {}", error);
      return;
    }
  }

  enabled_ = true;
}

void EvolveRuntime::prepare_statements() {
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  if (!con) return;

  struct Entry {
    const char* sql;
    void** out;
  };

  const Entry stmts[] = {
      {"INSERT INTO evolve_runs(run_id, session_key, experiment_name, "
       "run_spec_json, state, started_at, config_fingerprint) "
       "VALUES(?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_run_},
      {"UPDATE evolve_runs SET state=?, ended_at=CASE WHEN ? IN "
       "('completed','failed','orphaned') THEN ? ELSE ended_at END "
       "WHERE run_id=?;",
       &stmt_update_run_state_},
      {"UPDATE evolve_runs SET state=?, last_error=?, ended_at=CASE WHEN ? IN "
       "('completed','failed','orphaned') THEN ? ELSE ended_at END "
       "WHERE run_id=?;",
       &stmt_update_run_state_err_},
      {"UPDATE evolve_runs SET best_candidate_id=?, best_score=? "
       "WHERE run_id=?;",
       &stmt_update_run_best_},
      {"INSERT INTO evolve_candidates(candidate_id, run_id, round, parent_id, "
       "program_hash, program_path, motivation, state, created_at) "
       "VALUES(?, ?, ?, ?, ?, ?, ?, 'pending', ?);",
       &stmt_insert_candidate_},
      {"UPDATE evolve_candidates SET state=?, score=?, metrics_json=?, "
       "eval_wall_seconds=? WHERE candidate_id=?;",
       &stmt_update_candidate_eval_},
      {"INSERT INTO evolve_lessons(lesson_id, run_id, round, candidate_id, "
       "text, tags_json, created_at) VALUES(?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_lesson_},
      {"INSERT INTO evolve_cognition(cognition_id, run_id, source, title, "
       "content, tags_json, created_at) VALUES(?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_cognition_},
      {"INSERT INTO evolve_edges(edge_id, run_id, from_id, to_id, edge_type, "
       "metadata_json, created_at) VALUES(?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_edge_},
  };

  for (const auto& e : stmts) {
    duckdb_prepared_statement s;
    if (duckdb_prepare(con, e.sql, &s) == DuckDBError) {
      logger_->error("EvolveRuntime prepare failed: {}",
                     duckdb_prepare_error(s));
      duckdb_destroy_prepare(&s);
      enabled_ = false;
      return;
    }
    *e.out = s;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string EvolveRuntime::make_id(const std::string& prefix) const {
  thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<std::uint32_t> dist;
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::ostringstream oss;
  oss << prefix << "_" << ms << "_" << std::hex << dist(rng);
  return oss.str();
}

std::string EvolveRuntime::now_iso8601() const {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  gmtime_r(&tt, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

// ---------------------------------------------------------------------------
// Record operations
// ---------------------------------------------------------------------------

std::string EvolveRuntime::RecordRun(const std::string& experiment_name,
                                     const nlohmann::json& run_spec,
                                     const std::string& session_key,
                                     const std::string& config_fingerprint) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("run");
  auto now = now_iso8601();
  auto spec_str = run_spec.is_null() ? "{}" : run_spec.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_run_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  bind_str_or_null(stmt, 2, session_key);
  duckdb_bind_varchar(stmt, 3, experiment_name.c_str());
  duckdb_bind_varchar(stmt, 4, spec_str.c_str());
  duckdb_bind_varchar(stmt, 5, "pending");
  duckdb_bind_varchar(stmt, 6, now.c_str());
  bind_str_or_null(stmt, 7, config_fingerprint);

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime insert_run failed: {}",
                   duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return "";
  }
  duckdb_destroy_result(&result);
  return id;
}

void EvolveRuntime::UpdateRunState(const std::string& run_id,
                                   const std::string& new_state,
                                   const std::string& error_message) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto now = now_iso8601();

  if (error_message.empty()) {
    auto stmt = static_cast<duckdb_prepared_statement>(stmt_update_run_state_);
    if (!stmt) return;
    duckdb_bind_varchar(stmt, 1, new_state.c_str());
    duckdb_bind_varchar(stmt, 2, new_state.c_str());
    duckdb_bind_varchar(stmt, 3, now.c_str());
    duckdb_bind_varchar(stmt, 4, run_id.c_str());
    duckdb_result result;
    if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
      logger_->error("EvolveRuntime update_run_state failed: {}",
                     duckdb_result_error(&result));
    }
    duckdb_destroy_result(&result);
  } else {
    auto stmt =
        static_cast<duckdb_prepared_statement>(stmt_update_run_state_err_);
    if (!stmt) return;
    duckdb_bind_varchar(stmt, 1, new_state.c_str());
    duckdb_bind_varchar(stmt, 2, error_message.c_str());
    duckdb_bind_varchar(stmt, 3, new_state.c_str());
    duckdb_bind_varchar(stmt, 4, now.c_str());
    duckdb_bind_varchar(stmt, 5, run_id.c_str());
    duckdb_result result;
    if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
      logger_->error("EvolveRuntime update_run_state_err failed: {}",
                     duckdb_result_error(&result));
    }
    duckdb_destroy_result(&result);
  }
}

void EvolveRuntime::UpdateRunBest(const std::string& run_id,
                                  const std::string& best_candidate_id,
                                  double best_score) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto stmt = static_cast<duckdb_prepared_statement>(stmt_update_run_best_);
  if (!stmt) return;
  duckdb_bind_varchar(stmt, 1, best_candidate_id.c_str());
  duckdb_bind_double(stmt, 2, best_score);
  duckdb_bind_varchar(stmt, 3, run_id.c_str());
  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime update_run_best failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
}

std::string EvolveRuntime::RecordCandidate(const std::string& run_id,
                                           int round,
                                           const std::string& parent_id,
                                           const std::string& program_hash,
                                           const std::string& program_path,
                                           const std::string& motivation) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("cand");
  auto now = now_iso8601();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_candidate_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_int32(stmt, 3, round);
  bind_str_or_null(stmt, 4, parent_id);
  bind_str_or_null(stmt, 5, program_hash);
  bind_str_or_null(stmt, 6, program_path);
  bind_str_or_null(stmt, 7, motivation);
  duckdb_bind_varchar(stmt, 8, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime insert_candidate failed: {}",
                   duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return "";
  }
  duckdb_destroy_result(&result);
  return id;
}

void EvolveRuntime::UpdateCandidateEvaluation(
    const std::string& candidate_id, const std::string& state, double score,
    const nlohmann::json& metrics, double eval_wall_seconds) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto stmt =
      static_cast<duckdb_prepared_statement>(stmt_update_candidate_eval_);
  if (!stmt) return;

  auto metrics_str = metrics.is_null() ? "{}" : metrics.dump();
  duckdb_bind_varchar(stmt, 1, state.c_str());
  duckdb_bind_double(stmt, 2, score);
  duckdb_bind_varchar(stmt, 3, metrics_str.c_str());
  duckdb_bind_double(stmt, 4, eval_wall_seconds);
  duckdb_bind_varchar(stmt, 5, candidate_id.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime update_candidate_eval failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
}

std::string EvolveRuntime::RecordLesson(const std::string& run_id, int round,
                                        const std::string& candidate_id,
                                        const std::string& text,
                                        const nlohmann::json& tags) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("lsn");
  auto now = now_iso8601();
  auto tags_str = tags.is_null() ? "[]" : tags.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_lesson_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_int32(stmt, 3, round);
  bind_str_or_null(stmt, 4, candidate_id);
  duckdb_bind_varchar(stmt, 5, text.c_str());
  duckdb_bind_varchar(stmt, 6, tags_str.c_str());
  duckdb_bind_varchar(stmt, 7, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime insert_lesson failed: {}",
                   duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return "";
  }
  duckdb_destroy_result(&result);
  return id;
}

std::string EvolveRuntime::RecordCognition(const std::string& run_id,
                                           const std::string& source,
                                           const std::string& title,
                                           const std::string& content,
                                           const nlohmann::json& tags) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("cog");
  auto now = now_iso8601();
  auto tags_str = tags.is_null() ? "[]" : tags.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_cognition_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, source.c_str());
  bind_str_or_null(stmt, 4, title);
  duckdb_bind_varchar(stmt, 5, content.c_str());
  duckdb_bind_varchar(stmt, 6, tags_str.c_str());
  duckdb_bind_varchar(stmt, 7, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime insert_cognition failed: {}",
                   duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return "";
  }
  duckdb_destroy_result(&result);
  return id;
}

void EvolveRuntime::RecordEdge(const std::string& run_id,
                               const std::string& from_id,
                               const std::string& to_id,
                               const std::string& edge_type,
                               const nlohmann::json& metadata) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("edge");
  auto now = now_iso8601();
  auto meta_str = metadata.is_null() ? "{}" : metadata.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_edge_);
  if (!stmt) return;

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, from_id.c_str());
  duckdb_bind_varchar(stmt, 4, to_id.c_str());
  duckdb_bind_varchar(stmt, 5, edge_type.c_str());
  duckdb_bind_varchar(stmt, 6, meta_str.c_str());
  duckdb_bind_varchar(stmt, 7, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    logger_->error("EvolveRuntime insert_edge failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
}

int EvolveRuntime::MarkOrphanedRuns() {
  if (!enabled_) return 0;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  auto now = now_iso8601();

  // Count matching rows first (UPDATE does not reliably populate
  // duckdb_rows_changed on all versions).
  duckdb_result count_result;
  int count = 0;
  if (duckdb_query(con,
                   "SELECT COUNT(*) FROM evolve_runs "
                   "WHERE state IN ('pending','running');",
                   &count_result) != DuckDBError) {
    count = static_cast<int>(duckdb_value_int64(&count_result, 0, 0));
  }
  duckdb_destroy_result(&count_result);

  if (count == 0) return 0;

  std::string sql =
      "UPDATE evolve_runs SET state='orphaned', ended_at='" + now +
      "', last_error='gateway restarted while run was active' "
      "WHERE state IN ('pending','running');";
  duckdb_result result;
  if (duckdb_query(con, sql.c_str(), &result) == DuckDBError) {
    logger_->error("EvolveRuntime mark_orphans failed: {}",
                   duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return 0;
  }
  duckdb_destroy_result(&result);

  logger_->warn("EvolveRuntime: marked {} evolution run(s) as orphaned",
                count);
  return count;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

namespace {

std::string get_str(duckdb_result* result, idx_t col, idx_t row) {
  auto val = duckdb_value_varchar(result, col, row);
  std::string s = val ? val : "";
  duckdb_free(val);
  return s;
}

}  // namespace

nlohmann::json
EvolveRuntime::GetRunState(const std::string& run_id) const {
  if (!enabled_) return {};
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  std::string sql =
      "SELECT run_id, session_key, experiment_name, state, started_at, "
      "ended_at, best_candidate_id, best_score, last_error "
      "FROM evolve_runs WHERE run_id = '" +
      run_id + "';";

  duckdb_result result;
  if (duckdb_query(con, sql.c_str(), &result) == DuckDBError) {
    duckdb_destroy_result(&result);
    return {};
  }
  nlohmann::json out;
  if (duckdb_row_count(&result) > 0) {
    out["run_id"] = get_str(&result, 0, 0);
    out["session_key"] = get_str(&result, 1, 0);
    out["experiment_name"] = get_str(&result, 2, 0);
    out["state"] = get_str(&result, 3, 0);
    out["started_at"] = get_str(&result, 4, 0);
    out["ended_at"] = get_str(&result, 5, 0);
    out["best_candidate_id"] = get_str(&result, 6, 0);
    out["best_score"] = duckdb_value_double(&result, 7, 0);
    out["last_error"] = get_str(&result, 8, 0);
  }
  duckdb_destroy_result(&result);
  return out;
}

nlohmann::json
EvolveRuntime::GetBestCandidate(const std::string& run_id) const {
  if (!enabled_) return {};
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  std::string sql =
      "SELECT candidate_id, round, score, program_path, metrics_json "
      "FROM evolve_candidates WHERE run_id = '" +
      run_id + "' AND state = 'evaluated' "
               "ORDER BY score DESC, created_at ASC LIMIT 1;";

  duckdb_result result;
  if (duckdb_query(con, sql.c_str(), &result) == DuckDBError) {
    duckdb_destroy_result(&result);
    return {};
  }
  nlohmann::json out;
  if (duckdb_row_count(&result) > 0) {
    out["candidate_id"] = get_str(&result, 0, 0);
    out["round"] = duckdb_value_int32(&result, 1, 0);
    out["score"] = duckdb_value_double(&result, 2, 0);
    out["program_path"] = get_str(&result, 3, 0);
    out["metrics"] =
        nlohmann::json::parse(get_str(&result, 4, 0), nullptr, false);
  }
  duckdb_destroy_result(&result);
  return out;
}

nlohmann::json EvolveRuntime::ExportRun(const std::string& run_id) const {
  if (!enabled_) return {};
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  nlohmann::json out;

  // Run row (inlined to avoid recursive lock from GetRunState).
  {
    std::string sql =
        "SELECT run_id, state, started_at, ended_at, best_candidate_id, "
        "best_score, experiment_name FROM evolve_runs WHERE run_id = '" +
        run_id + "';";
    duckdb_result result;
    if (duckdb_query(con, sql.c_str(), &result) != DuckDBError &&
        duckdb_row_count(&result) > 0) {
      out["run"] = {
          {"run_id", get_str(&result, 0, 0)},
          {"state", get_str(&result, 1, 0)},
          {"started_at", get_str(&result, 2, 0)},
          {"ended_at", get_str(&result, 3, 0)},
          {"best_candidate_id", get_str(&result, 4, 0)},
          {"best_score", duckdb_value_double(&result, 5, 0)},
          {"experiment_name", get_str(&result, 6, 0)},
      };
    }
    duckdb_destroy_result(&result);
  }

  // Candidates
  {
    std::string sql =
        "SELECT candidate_id, round, parent_id, score, state, created_at "
        "FROM evolve_candidates WHERE run_id = '" +
        run_id + "' ORDER BY round ASC, created_at ASC;";
    duckdb_result result;
    if (duckdb_query(con, sql.c_str(), &result) != DuckDBError) {
      auto& arr = out["candidates"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        arr.push_back({
            {"candidate_id", get_str(&result, 0, i)},
            {"round", duckdb_value_int32(&result, 1, i)},
            {"parent_id", get_str(&result, 2, i)},
            {"score", duckdb_value_double(&result, 3, i)},
            {"state", get_str(&result, 4, i)},
            {"created_at", get_str(&result, 5, i)},
        });
      }
      duckdb_destroy_result(&result);
    }
  }

  // Lessons
  {
    std::string sql =
        "SELECT lesson_id, round, candidate_id, text, created_at "
        "FROM evolve_lessons WHERE run_id = '" +
        run_id + "' ORDER BY round ASC, created_at ASC;";
    duckdb_result result;
    if (duckdb_query(con, sql.c_str(), &result) != DuckDBError) {
      auto& arr = out["lessons"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        arr.push_back({
            {"lesson_id", get_str(&result, 0, i)},
            {"round", duckdb_value_int32(&result, 1, i)},
            {"candidate_id", get_str(&result, 2, i)},
            {"text", get_str(&result, 3, i)},
            {"created_at", get_str(&result, 4, i)},
        });
      }
      duckdb_destroy_result(&result);
    }
  }

  // Edges
  {
    std::string sql =
        "SELECT from_id, to_id, edge_type, created_at "
        "FROM evolve_edges WHERE run_id = '" +
        run_id + "' ORDER BY created_at ASC;";
    duckdb_result result;
    if (duckdb_query(con, sql.c_str(), &result) != DuckDBError) {
      auto& arr = out["edges"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        arr.push_back({
            {"from", get_str(&result, 0, i)},
            {"to", get_str(&result, 1, i)},
            {"type", get_str(&result, 2, i)},
            {"created_at", get_str(&result, 3, i)},
        });
      }
      duckdb_destroy_result(&result);
    }
  }

  return out;
}

}  // namespace quantclaw
