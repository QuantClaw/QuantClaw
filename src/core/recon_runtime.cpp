// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>
#include <duckdb.h>
#include <ctime>

module quantclaw.core.recon_runtime;

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;
import quantclaw.core.subagent;

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

// Severity ordering for auto-escalation threshold comparison.
int severity_rank(const std::string& severity) {
  if (severity == "critical") return 4;
  if (severity == "high") return 3;
  if (severity == "medium") return 2;
  if (severity == "low") return 1;
  return 0;  // "info"
}

}  // namespace

ReconRuntime::ReconRuntime(DagRuntime* dag_runtime,
                           std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)), dag_runtime_(dag_runtime) {
  if (!dag_runtime_ || !dag_runtime_->IsEnabled()) {
    logger_->warn("ReconRuntime: DagRuntime not available, recon graph disabled");
    return;
  }

  init_schema();
  if (enabled_) {
    prepare_statements();
  }

  if (enabled_) {
    logger_->info("ReconRuntime enabled, sharing DagRuntime DuckDB connection");
  }
}

ReconRuntime::~ReconRuntime() {
  if (!dag_runtime_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto destroy = [](void*& raw) {
    if (raw) {
      auto s = static_cast<duckdb_prepared_statement>(raw);
      duckdb_destroy_prepare(&s);
      raw = nullptr;
    }
  };

  destroy(stmt_insert_target_);
  destroy(stmt_update_target_seen_);
  destroy(stmt_insert_finding_);
  destroy(stmt_insert_probe_);
  destroy(stmt_complete_probe_);
  destroy(stmt_insert_edge_);
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

void ReconRuntime::init_schema() {
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  if (!con) return;

  std::string error;
  const char* kSchemaSql[] = {
      R"SQL(
CREATE TABLE IF NOT EXISTS recon_targets (
  target_id TEXT PRIMARY KEY,
  target TEXT NOT NULL,
  target_type TEXT NOT NULL,
  scope_status TEXT NOT NULL,
  first_seen TEXT NOT NULL,
  last_seen TEXT NOT NULL,
  metadata_json TEXT
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS recon_findings (
  finding_id TEXT PRIMARY KEY,
  run_id TEXT,
  target_id TEXT NOT NULL,
  severity TEXT NOT NULL,
  finding_type TEXT NOT NULL,
  evidence_json TEXT NOT NULL,
  tool_name TEXT,
  dag_node_id TEXT,
  created_at TEXT NOT NULL
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS recon_probes (
  probe_id TEXT PRIMARY KEY,
  run_id TEXT,
  source_finding_id TEXT,
  tool_name TEXT NOT NULL,
  target TEXT NOT NULL,
  parameters_json TEXT,
  result_summary TEXT,
  status TEXT NOT NULL,
  dag_node_id TEXT,
  created_at TEXT NOT NULL,
  completed_at TEXT
);
)SQL",
      R"SQL(
CREATE TABLE IF NOT EXISTS recon_edges (
  edge_id TEXT PRIMARY KEY,
  run_id TEXT,
  from_id TEXT NOT NULL,
  to_id TEXT NOT NULL,
  from_type TEXT NOT NULL,
  to_type TEXT NOT NULL,
  relationship TEXT NOT NULL,
  created_at TEXT NOT NULL
);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_recon_findings_target
  ON recon_findings(target_id);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_recon_findings_severity
  ON recon_findings(severity);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_recon_probes_source
  ON recon_probes(source_finding_id);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_recon_edges_from
  ON recon_edges(from_id);
)SQL",
      R"SQL(
CREATE INDEX IF NOT EXISTS idx_recon_edges_to
  ON recon_edges(to_id);
)SQL",
  };

  for (const auto* stmt : kSchemaSql) {
    if (!run_sql(con, stmt, &error)) {
      logger_->error("ReconRuntime schema init failed: {}", error);
      return;
    }
  }

  enabled_ = true;
}

void ReconRuntime::prepare_statements() {
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  if (!con) return;

  struct Entry {
    const char* sql;
    void** out;
  };

  const Entry stmts[] = {
      {"INSERT INTO recon_targets(target_id, target, target_type, scope_status, "
       "first_seen, last_seen, metadata_json) VALUES(?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_target_},
      {"UPDATE recon_targets SET last_seen=? WHERE target_id=?;",
       &stmt_update_target_seen_},
      {"INSERT INTO recon_findings(finding_id, run_id, target_id, severity, "
       "finding_type, evidence_json, tool_name, dag_node_id, created_at) "
       "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_finding_},
      {"INSERT INTO recon_probes(probe_id, run_id, source_finding_id, "
       "tool_name, target, parameters_json, status, created_at) "
       "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_probe_},
      {"UPDATE recon_probes SET status=?, result_summary=?, dag_node_id=?, "
       "completed_at=? WHERE probe_id=?;",
       &stmt_complete_probe_},
      {"INSERT INTO recon_edges(edge_id, run_id, from_id, to_id, from_type, "
       "to_type, relationship, created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
       &stmt_insert_edge_},
  };

  for (const auto& e : stmts) {
    duckdb_prepared_statement s;
    if (duckdb_prepare(con, e.sql, &s) == DuckDBError) {
      logger_->error("ReconRuntime prepare failed for '{}': {}", e.sql,
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

std::string ReconRuntime::make_id(const std::string& prefix) const {
  thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<std::uint32_t> dist;
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::ostringstream oss;
  oss << prefix << "_" << ms << "_" << std::hex << dist(rng);
  return oss.str();
}

std::string ReconRuntime::now_iso8601() const {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  gmtime_r(&tt, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

bool ReconRuntime::exec_sql(const std::string& sql) {
  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  std::string error;
  if (!run_sql(con, sql.c_str(), &error)) {
    logger_->error("ReconRuntime exec_sql failed: {}", error);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Record operations
// ---------------------------------------------------------------------------

std::string ReconRuntime::RecordTarget(const std::string& target,
                                       const std::string& target_type,
                                       const std::string& scope_status,
                                       const nlohmann::json& metadata) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("tgt");
  auto now = now_iso8601();
  auto meta_str = metadata.is_null() ? "{}" : metadata.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_target_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, target.c_str());
  duckdb_bind_varchar(stmt, 3, target_type.c_str());
  duckdb_bind_varchar(stmt, 4, scope_status.c_str());
  duckdb_bind_varchar(stmt, 5, now.c_str());
  duckdb_bind_varchar(stmt, 6, now.c_str());
  duckdb_bind_varchar(stmt, 7, meta_str.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError) {
    // Target may already exist — update last_seen instead.
    duckdb_destroy_result(&result);
    auto ustmt = static_cast<duckdb_prepared_statement>(stmt_update_target_seen_);
    if (ustmt) {
      duckdb_bind_varchar(ustmt, 1, now.c_str());
      duckdb_bind_varchar(ustmt, 2, id.c_str());
      duckdb_result uresult;
      duckdb_execute_prepared(ustmt, &uresult);
      duckdb_destroy_result(&uresult);
    }
    return id;
  }
  duckdb_destroy_result(&result);
  return id;
}

std::string ReconRuntime::RecordFinding(const std::string& target_id,
                                        const std::string& severity,
                                        const std::string& finding_type,
                                        const nlohmann::json& evidence,
                                        const std::string& tool_name,
                                        const std::string& run_id,
                                        const std::string& dag_node_id) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("fnd");
  auto now = now_iso8601();
  auto evidence_str = evidence.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_finding_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, target_id.c_str());
  duckdb_bind_varchar(stmt, 4, severity.c_str());
  duckdb_bind_varchar(stmt, 5, finding_type.c_str());
  duckdb_bind_varchar(stmt, 6, evidence_str.c_str());
  duckdb_bind_varchar(stmt, 7, tool_name.c_str());
  duckdb_bind_varchar(stmt, 8, dag_node_id.c_str());
  duckdb_bind_varchar(stmt, 9, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("ReconRuntime insert_finding failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);

  // Auto-escalation check (outside the lock would be ideal, but we need
  // the finding_id first).
  // We'll check after releasing the lock via MaybeAutoEscalate.
  return id;
}

std::string ReconRuntime::RecordProbe(const std::string& tool_name,
                                      const std::string& target,
                                      const nlohmann::json& parameters,
                                      const std::string& source_finding_id,
                                      const std::string& run_id) {
  if (!enabled_) return "";
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("prb");
  auto now = now_iso8601();
  auto params_str = parameters.dump();

  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_probe_);
  if (!stmt) return "";

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, source_finding_id.c_str());
  duckdb_bind_varchar(stmt, 4, tool_name.c_str());
  duckdb_bind_varchar(stmt, 5, target.c_str());
  duckdb_bind_varchar(stmt, 6, params_str.c_str());
  duckdb_bind_varchar(stmt, 7, "running");
  duckdb_bind_varchar(stmt, 8, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("ReconRuntime insert_probe failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
  return id;
}

void ReconRuntime::CompleteProbe(const std::string& probe_id,
                                 const std::string& status,
                                 const std::string& result_summary,
                                 const std::string& dag_node_id) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto now = now_iso8601();
  auto stmt = static_cast<duckdb_prepared_statement>(stmt_complete_probe_);
  if (!stmt) return;

  duckdb_bind_varchar(stmt, 1, status.c_str());
  duckdb_bind_varchar(stmt, 2, result_summary.c_str());
  duckdb_bind_varchar(stmt, 3, dag_node_id.c_str());
  duckdb_bind_varchar(stmt, 4, now.c_str());
  duckdb_bind_varchar(stmt, 5, probe_id.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("ReconRuntime complete_probe failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
}

void ReconRuntime::RecordEdge(const std::string& from_id,
                              const std::string& from_type,
                              const std::string& to_id,
                              const std::string& to_type,
                              const std::string& relationship,
                              const std::string& run_id) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto id = make_id("edge");
  auto now = now_iso8601();
  auto stmt = static_cast<duckdb_prepared_statement>(stmt_insert_edge_);
  if (!stmt) return;

  duckdb_bind_varchar(stmt, 1, id.c_str());
  duckdb_bind_varchar(stmt, 2, run_id.c_str());
  duckdb_bind_varchar(stmt, 3, from_id.c_str());
  duckdb_bind_varchar(stmt, 4, to_id.c_str());
  duckdb_bind_varchar(stmt, 5, from_type.c_str());
  duckdb_bind_varchar(stmt, 6, to_type.c_str());
  duckdb_bind_varchar(stmt, 7, relationship.c_str());
  duckdb_bind_varchar(stmt, 8, now.c_str());

  duckdb_result result;
  if (duckdb_execute_prepared(stmt, &result) == DuckDBError && logger_) {
    logger_->error("ReconRuntime insert_edge failed: {}",
                   duckdb_result_error(&result));
  }
  duckdb_destroy_result(&result);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

nlohmann::json ReconRuntime::QueryFindings(
    const std::string& target_id,
    const std::string& severity_filter) const {
  if (!enabled_) return nlohmann::json::array();
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  std::string sql =
      "SELECT finding_id, severity, finding_type, evidence_json, tool_name, "
      "created_at FROM recon_findings WHERE target_id = '" +
      target_id + "'";
  if (!severity_filter.empty()) {
    sql += " AND severity = '" + severity_filter + "'";
  }
  sql += " ORDER BY created_at DESC;";

  duckdb_result result;
  if (duckdb_query(con, sql.c_str(), &result) == DuckDBError) {
    duckdb_destroy_result(&result);
    return nlohmann::json::array();
  }

  nlohmann::json findings = nlohmann::json::array();
  auto row_count = duckdb_row_count(&result);
  for (std::uint64_t i = 0; i < row_count; ++i) {
    nlohmann::json f;
    auto get_str = [&](std::uint64_t col) -> std::string {
      auto val = duckdb_value_varchar(&result, col, i);
      std::string s = val ? val : "";
      duckdb_free(val);
      return s;
    };
    f["finding_id"] = get_str(0);
    f["severity"] = get_str(1);
    f["finding_type"] = get_str(2);
    f["evidence"] = nlohmann::json::parse(get_str(3), nullptr, false);
    f["tool_name"] = get_str(4);
    f["created_at"] = get_str(5);
    findings.push_back(f);
  }

  duckdb_destroy_result(&result);
  return findings;
}

nlohmann::json ReconRuntime::ExportGraph() const {
  if (!enabled_) return {};
  std::lock_guard<std::mutex> lock(dag_runtime_->GetMutex());

  auto con = static_cast<duckdb_connection>(dag_runtime_->GetConnection());
  nlohmann::json graph;

  // Export targets.
  {
    duckdb_result result;
    if (duckdb_query(con,
                     "SELECT target_id, target, target_type, scope_status "
                     "FROM recon_targets ORDER BY first_seen;",
                     &result) != DuckDBError) {
      auto& arr = graph["targets"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        auto get_str = [&](std::uint64_t col) -> std::string {
          auto val = duckdb_value_varchar(&result, col, i);
          std::string s = val ? val : "";
          duckdb_free(val);
          return s;
        };
        arr.push_back({{"target_id", get_str(0)},
                       {"target", get_str(1)},
                       {"type", get_str(2)},
                       {"scope", get_str(3)}});
      }
      duckdb_destroy_result(&result);
    }
  }

  // Export findings.
  {
    duckdb_result result;
    if (duckdb_query(con,
                     "SELECT finding_id, target_id, severity, finding_type, "
                     "evidence_json, tool_name FROM recon_findings "
                     "ORDER BY created_at;",
                     &result) != DuckDBError) {
      auto& arr = graph["findings"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        auto get_str = [&](std::uint64_t col) -> std::string {
          auto val = duckdb_value_varchar(&result, col, i);
          std::string s = val ? val : "";
          duckdb_free(val);
          return s;
        };
        arr.push_back({{"finding_id", get_str(0)},
                       {"target_id", get_str(1)},
                       {"severity", get_str(2)},
                       {"type", get_str(3)},
                       {"evidence", nlohmann::json::parse(get_str(4), nullptr, false)},
                       {"tool", get_str(5)}});
      }
      duckdb_destroy_result(&result);
    }
  }

  // Export edges.
  {
    duckdb_result result;
    if (duckdb_query(con,
                     "SELECT from_id, from_type, to_id, to_type, relationship "
                     "FROM recon_edges ORDER BY created_at;",
                     &result) != DuckDBError) {
      auto& arr = graph["edges"] = nlohmann::json::array();
      auto rows = duckdb_row_count(&result);
      for (std::uint64_t i = 0; i < rows; ++i) {
        auto get_str = [&](std::uint64_t col) -> std::string {
          auto val = duckdb_value_varchar(&result, col, i);
          std::string s = val ? val : "";
          duckdb_free(val);
          return s;
        };
        arr.push_back({{"from", get_str(0)},
                       {"from_type", get_str(1)},
                       {"to", get_str(2)},
                       {"to_type", get_str(3)},
                       {"relationship", get_str(4)}});
      }
      duckdb_destroy_result(&result);
    }
  }

  return graph;
}

// ---------------------------------------------------------------------------
// Auto-escalation
// ---------------------------------------------------------------------------

void ReconRuntime::SetSubagentManager(SubagentManager* manager,
                                      const std::string& session_key) {
  subagent_manager_ = manager;
  session_key_ = session_key;
}

void ReconRuntime::ConfigureAutoEscalation(const nlohmann::json& config) {
  if (!config.is_object()) return;
  auto_escalate_enabled_ = config.value("enabled", false);
  auto_escalate_min_severity_ = config.value("min_severity", "high");
  auto_escalate_model_ = config.value("model", "anthropic/claude-opus-4-6");
  logger_->info("Auto-escalation configured: enabled={}, min_severity={}, "
                "model={}",
                auto_escalate_enabled_, auto_escalate_min_severity_,
                auto_escalate_model_);
}

void ReconRuntime::MaybeAutoEscalate(const std::string& finding_id,
                                     const std::string& severity,
                                     const nlohmann::json& evidence) {
  if (!auto_escalate_enabled_ || !subagent_manager_) return;

  if (severity_rank(severity) < severity_rank(auto_escalate_min_severity_)) {
    return;
  }

  logger_->info("Auto-escalating finding {} (severity: {}) to {}",
                finding_id, severity, auto_escalate_model_);

  SpawnParams params;
  params.task = "Analyze this security finding in depth. Assess exploitability, "
                "suggest proof-of-concept approach, and estimate CVSS score.\n\n"
                "Finding ID: " + finding_id + "\n"
                "Severity: " + severity + "\n"
                "Evidence:\n" + evidence.dump(2);
  params.label = "Deep analysis: " + finding_id;
  params.model = auto_escalate_model_;
  params.thinking = "high";
  params.timeout_seconds = 600;
  params.mode = SpawnMode::kRun;
  params.cleanup = true;

  auto result = subagent_manager_->Spawn(params, session_key_);
  if (result.status == SpawnResult::Status::kAccepted) {
    logger_->info("Escalation subagent spawned: run_id={}", result.run_id);
  } else {
    logger_->error("Escalation subagent spawn failed: {}", result.error);
  }
}

}  // namespace quantclaw
