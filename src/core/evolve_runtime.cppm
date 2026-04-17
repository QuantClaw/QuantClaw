// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.evolve_runtime;

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;
import quantclaw.plugins.sidecar_manager;

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

  // -------------------------------------------------------------------------
  // M3: SidecarManager + RPC client + events.drain loop
  //
  // The Python ASI-Evolve bridge runs under a dedicated SidecarManager
  // instance. EvolveRuntime marshals RPC calls to it (SidecarCall) and
  // polls for asynchronous events (PollEvents) which it routes to DagRuntime
  // using a per-run DagTurnState bound via AttachRun/DetachRun.
  // -------------------------------------------------------------------------

  // Start the Python bridge under a new SidecarManager instance. Reads
  // python_exe / bridge_script / heartbeat_* / max_restarts / plugin_config
  // from evolve_config. Returns false if config is incomplete or the
  // subprocess fails to start.
  bool StartSidecar(const nlohmann::json& evolve_config);

  // Stop events thread, stop sidecar, clear state. Idempotent.
  void StopSidecar();

  // True iff sidecar process is alive.
  bool IsSidecarRunning() const;

  // JSON-RPC 2.0 call through the sidecar. Returns an error response (ok=false)
  // when the sidecar is not started rather than throwing.
  SidecarResponse SidecarCall(const std::string& method,
                              const nlohmann::json& params,
                              int timeout_ms = 30000);

  // Drain any pending events from the sidecar (events.drain RPC) and route
  // them to DagRuntime::EmitNode for bound runs. Called automatically by the
  // background events thread when the poll interval is > 0; exposed publicly
  // so tests can drive it deterministically.
  void PollEvents();

  // Bind a run_id to a DAG turn so subsequent events for this run emit nodes
  // under the same DAG run. Called by the evolve_start tool after run.init
  // succeeds. No-op if DagRuntime is not enabled. Idempotent — subsequent
  // calls with the same run_id return false.
  bool AttachRun(const std::string& run_id,
                 const std::string& experiment_name);

  // Finalize the DAG turn for this run. status ∈ {"completed","stopped",
  // "error"}. Removes the entry from the in-memory turn map. No-op if run_id
  // was never attached.
  void DetachRun(const std::string& run_id, const std::string& status,
                 const std::string& error_message = "");

  // Number of runs currently attached to a DAG turn. For tests/observability.
  std::size_t AttachedRunCount() const;

 private:
  void init_schema();
  void prepare_statements();
  std::string make_id(const std::string& prefix) const;
  std::string now_iso8601() const;

  // M3: events loop body.
  void events_loop_();

  // M3: dispatch one drained event onto the appropriate DagTurnState.
  void route_event_(const nlohmann::json& event);

  std::shared_ptr<spdlog::logger> logger_;
  DagRuntime* dag_runtime_ = nullptr;
  bool enabled_ = false;

  // M3: sidecar + events.
  std::unique_ptr<SidecarManager> sidecar_;
  std::thread events_thread_;
  std::atomic<bool> events_stop_{false};
  int events_poll_interval_ms_ = 500;

  // M3: per-run DAG turn states, keyed by evolve run_id.
  mutable std::mutex turn_map_mu_;
  std::unordered_map<std::string, DagTurnState> run_turn_states_;

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
