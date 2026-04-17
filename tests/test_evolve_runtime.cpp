// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <gtest/gtest.h>
#include <duckdb.h>

import std;
import nlohmann.json;
import quantclaw.core.dag_runtime;
import quantclaw.core.evolve_runtime;
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

class EvolveRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_evolve_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    db_path_ = test_dir_ / "evolve.duckdb";
    dag_ = std::make_unique<quantclaw::DagRuntime>(db_path_.string(), logger_);
    evolve_ =
        std::make_unique<quantclaw::EvolveRuntime>(dag_.get(), logger_);
  }

  void TearDown() override {
    evolve_.reset();
    dag_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::filesystem::path db_path_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::DagRuntime> dag_;
  std::unique_ptr<quantclaw::EvolveRuntime> evolve_;
};

TEST_F(EvolveRuntimeTest, IsEnabledWhenDagRuntimeIsValid) {
  EXPECT_TRUE(evolve_->IsEnabled());
}

TEST_F(EvolveRuntimeTest, SchemaCreatesAllTables) {
  auto con =
      static_cast<duckdb_connection>(dag_->GetConnection());
  const char* kTables[] = {"evolve_runs", "evolve_candidates",
                           "evolve_lessons", "evolve_cognition",
                           "evolve_edges"};
  for (const auto* name : kTables) {
    std::string sql =
        std::string("SELECT COUNT(*) FROM information_schema.tables "
                    "WHERE table_name = '") +
        name + "';";
    EXPECT_EQ(scalar_count(con, sql), 1) << "missing table: " << name;
  }
}

TEST_F(EvolveRuntimeTest, RecordRunReturnsIdInPendingState) {
  auto id = evolve_->RecordRun("qc_smoke",
                               nlohmann::json{{"max_rounds", 3}}, "sess-1");
  EXPECT_FALSE(id.empty());
  EXPECT_EQ(id.substr(0, 4), "run_");
  auto state = evolve_->GetRunState(id);
  EXPECT_EQ(state["state"].get<std::string>(), "pending");
  EXPECT_EQ(state["experiment_name"].get<std::string>(), "qc_smoke");
}

TEST_F(EvolveRuntimeTest, UpdateRunStateTransitions) {
  auto id = evolve_->RecordRun("qc_smoke", {});
  evolve_->UpdateRunState(id, "running");
  EXPECT_EQ(evolve_->GetRunState(id)["state"].get<std::string>(), "running");
  evolve_->UpdateRunState(id, "failed", "evaluator timed out");
  auto final_state = evolve_->GetRunState(id);
  EXPECT_EQ(final_state["state"].get<std::string>(), "failed");
  EXPECT_EQ(final_state["last_error"].get<std::string>(),
            "evaluator timed out");
  EXPECT_FALSE(final_state["ended_at"].get<std::string>().empty());
}

TEST_F(EvolveRuntimeTest, CandidateLifecycleAndBestSelection) {
  auto run_id = evolve_->RecordRun("qc_smoke", {});
  auto c1 = evolve_->RecordCandidate(run_id, 0, "", "hashA", "/tmp/a.py",
                                     "baseline");
  auto c2 = evolve_->RecordCandidate(run_id, 1, c1, "hashB", "/tmp/b.py",
                                     "mutation");
  EXPECT_FALSE(c1.empty());
  EXPECT_FALSE(c2.empty());

  evolve_->UpdateCandidateEvaluation(c1, "evaluated", 0.5, {{"runs", 10}},
                                     1.2);
  evolve_->UpdateCandidateEvaluation(c2, "evaluated", 0.9, {{"runs", 10}},
                                     1.5);

  auto best = evolve_->GetBestCandidate(run_id);
  EXPECT_EQ(best["candidate_id"].get<std::string>(), c2);
  EXPECT_DOUBLE_EQ(best["score"].get<double>(), 0.9);
}

TEST_F(EvolveRuntimeTest, LessonsAndCognitionArePersisted) {
  auto run_id = evolve_->RecordRun("qc_smoke", {});
  auto cand = evolve_->RecordCandidate(run_id, 0, "", "h", "/tmp/p.py", "m");

  auto lsn = evolve_->RecordLesson(run_id, 0, cand, "prefer early-exit",
                                   nlohmann::json::array({"perf"}));
  EXPECT_FALSE(lsn.empty());
  EXPECT_EQ(lsn.substr(0, 4), "lsn_");

  auto cog = evolve_->RecordCognition(run_id, "seed", "Quicksort notes",
                                      "Partitioning is the hot path",
                                      nlohmann::json::array({"algo"}));
  EXPECT_FALSE(cog.empty());
  EXPECT_EQ(cog.substr(0, 4), "cog_");
}

TEST_F(EvolveRuntimeTest, EdgesRecordLineage) {
  auto run_id = evolve_->RecordRun("qc_smoke", {});
  auto c1 = evolve_->RecordCandidate(run_id, 0, "", "h1", "/tmp/1", "seed");
  auto c2 = evolve_->RecordCandidate(run_id, 1, c1, "h2", "/tmp/2", "mut");
  evolve_->RecordEdge(run_id, c1, c2, "parent_of");

  auto con = static_cast<duckdb_connection>(dag_->GetConnection());
  EXPECT_EQ(scalar_count(con,
                         "SELECT COUNT(*) FROM evolve_edges WHERE "
                         "edge_type = 'parent_of';"),
            1);
}

TEST_F(EvolveRuntimeTest, MarkOrphanedRunsCleansRunningAndPending) {
  auto r1 = evolve_->RecordRun("qc_smoke", {});
  auto r2 = evolve_->RecordRun("qc_smoke", {});
  auto r3 = evolve_->RecordRun("qc_smoke", {});
  evolve_->UpdateRunState(r2, "running");
  evolve_->UpdateRunState(r3, "completed");

  int marked = evolve_->MarkOrphanedRuns();
  EXPECT_EQ(marked, 2);
  EXPECT_EQ(evolve_->GetRunState(r1)["state"].get<std::string>(), "orphaned");
  EXPECT_EQ(evolve_->GetRunState(r2)["state"].get<std::string>(), "orphaned");
  EXPECT_EQ(evolve_->GetRunState(r3)["state"].get<std::string>(), "completed");
}

TEST_F(EvolveRuntimeTest, ExportRunReturnsFullGraph) {
  auto run_id = evolve_->RecordRun("qc_smoke", {});
  auto c1 = evolve_->RecordCandidate(run_id, 0, "", "h", "/tmp/a", "seed");
  evolve_->UpdateCandidateEvaluation(c1, "evaluated", 0.7, {}, 1.0);
  evolve_->RecordLesson(run_id, 0, c1, "note", {});
  evolve_->RecordEdge(run_id, c1, "ext", "lesson_from");

  auto exported = evolve_->ExportRun(run_id);
  EXPECT_EQ(exported["candidates"].size(), 1);
  EXPECT_EQ(exported["lessons"].size(), 1);
  EXPECT_EQ(exported["edges"].size(), 1);
}
