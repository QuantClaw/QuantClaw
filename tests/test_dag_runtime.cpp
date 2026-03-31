// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <gtest/gtest.h>
#include <duckdb.h>

import std;
import quantclaw.providers.llm_provider;
import nlohmann.json;

import quantclaw.core.dag_runtime;
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

class DagRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_dag_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    db_path_ = test_dir_ / "dag.duckdb";
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
  std::filesystem::path db_path_;
  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(DagRuntimeTest, BeginEmitEndPersistsRunNodeAndEdge) {
  quantclaw::DagRuntime runtime(db_path_.string(), logger_);
  ASSERT_TRUE(runtime.IsEnabled());

  auto turn = runtime.BeginTurn("agent:main:main", "hello");
  ASSERT_FALSE(turn.run_id.empty());

  runtime.EmitNode(&turn, quantclaw::DagNodeType::kContextAssembled,
                   nlohmann::json{{"messages", 3}});
  runtime.EndTurn(&turn, "completed");

  duckdb_database db = nullptr;
  ASSERT_EQ(duckdb_open(db_path_.string().c_str(), &db), DuckDBSuccess);
  duckdb_connection con = nullptr;
  ASSERT_EQ(duckdb_connect(db, &con), DuckDBSuccess);

  EXPECT_EQ(scalar_count(con, "SELECT COUNT(*) FROM dag_runs;"), 1);
  EXPECT_EQ(scalar_count(con, "SELECT COUNT(*) FROM dag_nodes;"), 2);
  EXPECT_EQ(scalar_count(con, "SELECT COUNT(*) FROM dag_edges;"), 1);
  EXPECT_EQ(
      scalar_count(con, "SELECT COUNT(*) FROM dag_runs WHERE status='completed';"),
      1);

  duckdb_disconnect(&con);
  duckdb_close(&db);
}

TEST_F(DagRuntimeTest, LatestRunIdForSessionReturnsMostRecentRun) {
  quantclaw::DagRuntime runtime(db_path_.string(), logger_);
  ASSERT_TRUE(runtime.IsEnabled());

  auto turn1 = runtime.BeginTurn("agent:main:main", "hello");
  runtime.EndTurn(&turn1, "completed");
  auto turn2 = runtime.BeginTurn("agent:main:main", "hello again");
  runtime.EndTurn(&turn2, "completed");

  EXPECT_EQ(runtime.LatestRunIdForSession("agent:main:main"), turn2.run_id);
}

TEST_F(DagRuntimeTest, MemoryManagementNodeTypePersists) {
  quantclaw::DagRuntime runtime(db_path_.string(), logger_);
  ASSERT_TRUE(runtime.IsEnabled());

  EXPECT_EQ(
      quantclaw::DagNodeTypeToString(quantclaw::DagNodeType::kMemoryManagement),
      "memory_management");

  auto turn = runtime.BeginTurn("agent:main:main", "hello");
  ASSERT_FALSE(turn.run_id.empty());
  runtime.EmitNode(&turn, quantclaw::DagNodeType::kMemoryManagement,
                   nlohmann::json{{"iteration", 1}, {"requestMessages", 2}});
  runtime.EndTurn(&turn, "completed");

  duckdb_database db = nullptr;
  ASSERT_EQ(duckdb_open(db_path_.string().c_str(), &db), DuckDBSuccess);
  duckdb_connection con = nullptr;
  ASSERT_EQ(duckdb_connect(db, &con), DuckDBSuccess);

  EXPECT_EQ(scalar_count(
                con,
                "SELECT COUNT(*) FROM dag_nodes WHERE "
                "node_type='memory_management';"),
            1);

  duckdb_disconnect(&con);
  duckdb_close(&db);
}
