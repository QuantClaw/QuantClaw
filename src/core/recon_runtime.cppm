// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.recon_runtime;

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;
import quantclaw.core.subagent;

export namespace quantclaw {

// Recon-specific DuckDB tables for bug bounty reconnaissance.
// Shares the DuckDB connection from DagRuntime, adding 4 tables:
//   recon_targets  — discovered targets (domain/IP/URL, scope status)
//   recon_findings — vulnerability findings with severity and evidence
//   recon_probes   — scan execution records
//   recon_edges    — "this discovery led to that probe" graph edges
class ReconRuntime {
 public:
  ReconRuntime(DagRuntime* dag_runtime,
               std::shared_ptr<spdlog::logger> logger);
  ~ReconRuntime();

  bool IsEnabled() const { return enabled_; }

  // Record a discovered target. Returns target_id.
  std::string RecordTarget(const std::string& target,
                           const std::string& target_type,
                           const std::string& scope_status,
                           const nlohmann::json& metadata = {});

  // Record a finding. Returns finding_id.
  std::string RecordFinding(const std::string& target_id,
                            const std::string& severity,
                            const std::string& finding_type,
                            const nlohmann::json& evidence,
                            const std::string& tool_name = "",
                            const std::string& run_id = "",
                            const std::string& dag_node_id = "");

  // Record a probe (scan execution). Returns probe_id.
  std::string RecordProbe(const std::string& tool_name,
                          const std::string& target,
                          const nlohmann::json& parameters,
                          const std::string& source_finding_id = "",
                          const std::string& run_id = "");

  // Complete a probe with results.
  void CompleteProbe(const std::string& probe_id,
                     const std::string& status,
                     const std::string& result_summary = "",
                     const std::string& dag_node_id = "");

  // Link two items in the recon graph.
  void RecordEdge(const std::string& from_id,
                  const std::string& from_type,
                  const std::string& to_id,
                  const std::string& to_type,
                  const std::string& relationship,
                  const std::string& run_id = "");

  // Query findings for a target, optionally filtered by severity.
  nlohmann::json QueryFindings(const std::string& target_id,
                               const std::string& severity_filter = "") const;

  // Export the full recon graph as JSON for reporting.
  nlohmann::json ExportGraph() const;

  // Set subagent manager for auto-escalation.
  void SetSubagentManager(SubagentManager* manager,
                          const std::string& session_key = "");

  // Configure auto-escalation settings.
  void ConfigureAutoEscalation(const nlohmann::json& config);

  // Check if a finding should be auto-escalated, and if so, trigger it.
  void MaybeAutoEscalate(const std::string& finding_id,
                         const std::string& severity,
                         const nlohmann::json& evidence);

 private:
  void init_schema();
  void prepare_statements();
  std::string make_id(const std::string& prefix) const;
  std::string now_iso8601() const;

  // Execute SQL directly on the shared connection (for schema init).
  bool exec_sql(const std::string& sql);

  std::shared_ptr<spdlog::logger> logger_;
  DagRuntime* dag_runtime_ = nullptr;
  bool enabled_ = false;

  // Prepared statements (void* to avoid duckdb header exposure).
  void* stmt_insert_target_ = nullptr;
  void* stmt_update_target_seen_ = nullptr;
  void* stmt_insert_finding_ = nullptr;
  void* stmt_insert_probe_ = nullptr;
  void* stmt_complete_probe_ = nullptr;
  void* stmt_insert_edge_ = nullptr;

  // Auto-escalation
  SubagentManager* subagent_manager_ = nullptr;
  std::string session_key_;
  bool auto_escalate_enabled_ = false;
  std::string auto_escalate_min_severity_ = "high";
  std::string auto_escalate_model_ = "anthropic/claude-opus-4-6";
};

}  // namespace quantclaw
