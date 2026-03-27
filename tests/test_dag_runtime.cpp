// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import std;
import <spdlog/sinks/null_sink.h>;
import <spdlog/spdlog.h>;

import <sqlite3.h>;

import quantclaw.core.dag_runtime;
import quantclaw.test.helpers;
import <gtest/gtest.h>;

namespace {

int scalar_count(sqlite3* db, const std::string& sql) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }
  int value = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    value = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return value;
}

}  // namespace

class DagRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_dag_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    db_path_ = test_dir_ / "dag.sqlite3";
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

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(db_path_.string().c_str(), &db, SQLITE_OPEN_READONLY,
                            nullptr),
            SQLITE_OK);

  EXPECT_EQ(scalar_count(db, "SELECT COUNT(*) FROM dag_runs;"), 1);
  EXPECT_EQ(scalar_count(db, "SELECT COUNT(*) FROM dag_nodes;"), 2);
  EXPECT_EQ(scalar_count(db, "SELECT COUNT(*) FROM dag_edges;"), 1);
  EXPECT_EQ(
      scalar_count(db, "SELECT COUNT(*) FROM dag_runs WHERE status='completed';"),
      1);

  sqlite3_close(db);
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

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(db_path_.string().c_str(), &db, SQLITE_OPEN_READONLY,
                            nullptr),
            SQLITE_OK);

  EXPECT_EQ(scalar_count(
                db,
                "SELECT COUNT(*) FROM dag_nodes WHERE "
                "node_type='memory_management';"),
            1);

  sqlite3_close(db);
}
