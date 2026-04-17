// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.evolve_runtime;

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;

export namespace quantclaw {

// Evolution-specific DuckDB tables for ASI-Evolve integration.
// Shares the DuckDB connection from DagRuntime, adding 5 tables:
//   evolve_runs       — one row per evolution run
//   evolve_candidates — one row per generated+evaluated candidate
//   evolve_lessons    — analyzer output (with optional embeddings)
//   evolve_cognition  — mirror of ASI-Evolve's cognition store
//   evolve_edges      — lineage edges across candidates/lessons/cognition
//
// Design notes for M1 (skeleton):
//  * No sidecar integration yet (M3).
//  * Runs are session-independent: session_key is nullable and informational.
//  * State model assumes asynchronous step semantics: candidates may exist in
//    state=pending/running before evaluation completes.
class EvolveRuntime {
 public:
  EvolveRuntime(DagRuntime* dag_runtime,
                std::shared_ptr<spdlog::logger> logger);
  ~EvolveRuntime();

  EvolveRuntime(const EvolveRuntime&) = delete;
  EvolveRuntime& operator=(const EvolveRuntime&) = delete;

  bool IsEnabled() const { return enabled_; }

  // Insert a new run in state=pending. Returns run_id.
  std::string RecordRun(const std::string& experiment_name,
                        const nlohmann::json& run_spec,
                        const std::string& session_key = "",
                        const std::string& config_fingerprint = "");

  // Transition an existing run's state. Optional ended_at set when terminal.
  void UpdateRunState(const std::string& run_id, const std::string& new_state,
                      const std::string& error_message = "");

  // Update the cached best candidate for a run.
  void UpdateRunBest(const std::string& run_id,
                     const std::string& best_candidate_id, double best_score);

  // Insert a new candidate in state=pending. Returns candidate_id.
  std::string RecordCandidate(const std::string& run_id, int round,
                              const std::string& parent_id,
                              const std::string& program_hash,
                              const std::string& program_path,
                              const std::string& motivation);

  // Update a candidate after evaluation completes or fails.
  void UpdateCandidateEvaluation(const std::string& candidate_id,
                                 const std::string& state, double score,
                                 const nlohmann::json& metrics,
                                 double eval_wall_seconds);

  // Insert a lesson. Embedding populated separately by memory bridge (M5).
  std::string RecordLesson(const std::string& run_id, int round,
                           const std::string& candidate_id,
                           const std::string& text,
                           const nlohmann::json& tags);

  // Insert a cognition item mirrored from ASI-Evolve's cognition store.
  std::string RecordCognition(const std::string& run_id,
                              const std::string& source,
                              const std::string& title,
                              const std::string& content,
                              const nlohmann::json& tags);

  // Insert a lineage edge (parent_of, lesson_from, cognition_used_in,
  // dag_node_emitted_for).
  void RecordEdge(const std::string& run_id, const std::string& from_id,
                  const std::string& to_id, const std::string& edge_type,
                  const nlohmann::json& metadata = {});

  // Scan evolve_runs for rows left in state=running after a gateway restart
  // and transition them to state=orphaned. Called at gateway startup.
  int MarkOrphanedRuns();

  // Query current run state + cached best candidate.
  nlohmann::json GetRunState(const std::string& run_id) const;

  // Return the best candidate for a run (by score, with ties broken by
  // earliest created_at).
  nlohmann::json GetBestCandidate(const std::string& run_id) const;

  // Export a full run (run row + candidates + lessons + edges) as JSON.
  nlohmann::json ExportRun(const std::string& run_id) const;

 private:
  void init_schema();
  void prepare_statements();
  std::string make_id(const std::string& prefix) const;
  std::string now_iso8601() const;

  std::shared_ptr<spdlog::logger> logger_;
  DagRuntime* dag_runtime_ = nullptr;
  bool enabled_ = false;

  // Prepared statements (void* to avoid duckdb header exposure).
  void* stmt_insert_run_ = nullptr;
  void* stmt_update_run_state_ = nullptr;
  void* stmt_update_run_state_err_ = nullptr;
  void* stmt_update_run_best_ = nullptr;
  void* stmt_insert_candidate_ = nullptr;
  void* stmt_update_candidate_eval_ = nullptr;
  void* stmt_insert_lesson_ = nullptr;
  void* stmt_insert_cognition_ = nullptr;
  void* stmt_insert_edge_ = nullptr;
};

}  // namespace quantclaw
