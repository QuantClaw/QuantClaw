// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <gtest/gtest.h>
#include <duckdb.h>

import std;
import nlohmann.json;
import quantclaw.providers.llm_provider;
import quantclaw.core.dag_runtime;
import quantclaw.core.recon_runtime;
import quantclaw.test.helpers;

namespace {

int scalar_count(duckdb_connection con, const std::string& sql) {
  duckdb_result result;
  if (duckdb_query(con, sql.c_str(), &result) == DuckDBError) {
    duckdb_destroy_result(&result);
    return -1;
  }
  auto value = static_cast<int>(duckdb_value_int64(&result, 0, 0));
  duckdb_destroy_result(&result);
  return value;
}

}  // namespace

class ReconRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_recon_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    db_path_ = test_dir_ / "recon.duckdb";
    dag_ = std::make_unique<quantclaw::DagRuntime>(db_path_.string(), logger_);
    recon_ = std::make_unique<quantclaw::ReconRuntime>(dag_.get(), logger_);
  }

  void TearDown() override {
    recon_.reset();
    dag_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::filesystem::path db_path_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::DagRuntime> dag_;
  std::unique_ptr<quantclaw::ReconRuntime> recon_;
};

TEST_F(ReconRuntimeTest, IsEnabledWhenDagRuntimeIsValid) {
  EXPECT_TRUE(recon_->IsEnabled());
}

TEST_F(ReconRuntimeTest, RecordTargetReturnsId) {
  auto id = recon_->RecordTarget("example.com", "domain", "in_scope");
  EXPECT_FALSE(id.empty());
  EXPECT_EQ(id.substr(0, 4), "tgt_");
}

TEST_F(ReconRuntimeTest, RecordFindingReturnsId) {
  auto target_id = recon_->RecordTarget("example.com", "domain", "in_scope");
  auto finding_id = recon_->RecordFinding(
      target_id, "high", "open_redirect",
      nlohmann::json{{"url", "https://example.com/redir?to=evil.com"}},
      "header_analysis");
  EXPECT_FALSE(finding_id.empty());
  EXPECT_EQ(finding_id.substr(0, 4), "fnd_");
}

TEST_F(ReconRuntimeTest, RecordAndCompleteProbe) {
  auto probe_id = recon_->RecordProbe(
      "port_scan", "example.com",
      nlohmann::json{{"ports", "top1000"}});
  EXPECT_FALSE(probe_id.empty());
  EXPECT_EQ(probe_id.substr(0, 4), "prb_");

  // Should not throw
  recon_->CompleteProbe(probe_id, "completed", "22,80,443 open");
}

TEST_F(ReconRuntimeTest, RecordEdgeCreatesRelationship) {
  auto target_id = recon_->RecordTarget("example.com", "domain", "in_scope");
  auto probe_id = recon_->RecordProbe(
      "subdomain_enum", "example.com", nlohmann::json{});

  // Should not throw
  recon_->RecordEdge(target_id, "target", probe_id, "probe",
                     "triggered_scan");
}

TEST_F(ReconRuntimeTest, QueryFindingsReturnsResults) {
  auto target_id = recon_->RecordTarget("example.com", "domain", "in_scope");
  recon_->RecordFinding(target_id, "high", "xss",
                        nlohmann::json{{"payload", "<script>"}},
                        "nuclei_scan");
  recon_->RecordFinding(target_id, "info", "tech_detect",
                        nlohmann::json{{"server", "nginx"}},
                        "header_analysis");

  auto all = recon_->QueryFindings(target_id);
  EXPECT_EQ(all.size(), 2u);

  auto high_only = recon_->QueryFindings(target_id, "high");
  EXPECT_EQ(high_only.size(), 1u);
  EXPECT_EQ(high_only[0]["severity"], "high");
}

TEST_F(ReconRuntimeTest, ExportGraphContainsAllComponents) {
  auto target_id = recon_->RecordTarget("example.com", "domain", "in_scope");
  auto finding_id = recon_->RecordFinding(
      target_id, "medium", "missing_hsts",
      nlohmann::json{{"header", "Strict-Transport-Security"}},
      "header_analysis");
  recon_->RecordEdge(target_id, "target", finding_id, "finding",
                     "produced_finding");

  auto graph = recon_->ExportGraph();
  EXPECT_TRUE(graph.contains("targets"));
  EXPECT_TRUE(graph.contains("findings"));
  EXPECT_TRUE(graph.contains("edges"));
  EXPECT_GE(graph["targets"].size(), 1u);
  EXPECT_GE(graph["findings"].size(), 1u);
  EXPECT_GE(graph["edges"].size(), 1u);
}

TEST_F(ReconRuntimeTest, TablesExistInSharedDatabase) {
  // Verify tables were created in the DuckDB shared with DagRuntime
  auto* db = dag_->GetDatabase();
  auto* con = dag_->GetConnection();
  ASSERT_NE(db, nullptr);
  ASSERT_NE(con, nullptr);

  auto dcon = static_cast<duckdb_connection>(con);
  EXPECT_GE(scalar_count(dcon, "SELECT COUNT(*) FROM recon_targets;"), 0);
  EXPECT_GE(scalar_count(dcon, "SELECT COUNT(*) FROM recon_findings;"), 0);
  EXPECT_GE(scalar_count(dcon, "SELECT COUNT(*) FROM recon_probes;"), 0);
  EXPECT_GE(scalar_count(dcon, "SELECT COUNT(*) FROM recon_edges;"), 0);
}
