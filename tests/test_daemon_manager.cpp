// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/gateway/daemon_manager.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

using namespace quantclaw::gateway;

class DaemonManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use a temp directory as HOME so we don't touch the real system
    test_home_ = quantclaw::test::MakeTestDir("quantclaw_daemon_test");

    original_home_ = std::getenv("HOME") ? std::getenv("HOME") : "";
    setenv("HOME", test_home_.c_str(), 1);

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_daemon", null_sink);

    daemon_ = std::make_unique<DaemonManager>(logger_);
  }

  void TearDown() override {
    daemon_.reset();
    // Restore HOME
    if (!original_home_.empty()) {
      setenv("HOME", original_home_.c_str(), 1);
    }
    if (std::filesystem::exists(test_home_)) {
      std::filesystem::remove_all(test_home_);
    }
  }

  // Write a PID file directly for testing
  void write_pid_file(int pid) {
    auto pid_path = test_home_ / ".quantclaw" / "gateway.pid";
    std::filesystem::create_directories(pid_path.parent_path());
    std::ofstream f(pid_path);
    f << pid;
    f.close();
  }

  std::filesystem::path test_home_;
  std::string original_home_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<DaemonManager> daemon_;
};

// --- Constructor ---

TEST_F(DaemonManagerTest, ConstructorCreatesDirectories) {
  auto logs_dir = test_home_ / ".quantclaw" / "logs";
  EXPECT_TRUE(std::filesystem::exists(logs_dir));
  EXPECT_TRUE(std::filesystem::is_directory(logs_dir));
}

TEST_F(DaemonManagerTest, ConstructorCreatesQuantclawDir) {
  auto qc_dir = test_home_ / ".quantclaw";
  EXPECT_TRUE(std::filesystem::exists(qc_dir));
}

// --- get_pid ---

TEST_F(DaemonManagerTest, GetPidNoPidFile) {
  EXPECT_EQ(daemon_->GetPid(), -1);
}

TEST_F(DaemonManagerTest, GetPidValidPidFile) {
  write_pid_file(12345);
  EXPECT_EQ(daemon_->GetPid(), 12345);
}

TEST_F(DaemonManagerTest, GetPidEmptyFile) {
  auto pid_path = test_home_ / ".quantclaw" / "gateway.pid";
  std::ofstream f(pid_path);
  f << "";
  f.close();
  // Empty file → extraction fails, pid stays -1
  EXPECT_EQ(daemon_->GetPid(), -1);
}

TEST_F(DaemonManagerTest, GetPidInvalidContent) {
  auto pid_path = test_home_ / ".quantclaw" / "gateway.pid";
  std::ofstream f(pid_path);
  f << "not_a_number";
  f.close();
  // Non-numeric → extraction fails, pid stays -1
  int pid = daemon_->GetPid();
  EXPECT_LE(pid, 0);
}

// --- is_running ---

TEST_F(DaemonManagerTest, IsRunningNoPidFile) {
  EXPECT_FALSE(daemon_->IsRunning());
}

TEST_F(DaemonManagerTest, IsRunningCurrentProcess) {
  // Write our own PID — the current process is definitely running
  write_pid_file(getpid());
  EXPECT_TRUE(daemon_->IsRunning());
}

TEST_F(DaemonManagerTest, IsRunningDeadProcess) {
  // PID 999999999 almost certainly doesn't exist
  write_pid_file(999999999);
  EXPECT_FALSE(daemon_->IsRunning());
}

TEST_F(DaemonManagerTest, IsRunningNegativePid) {
  write_pid_file(-1);
  EXPECT_FALSE(daemon_->IsRunning());
}

TEST_F(DaemonManagerTest, IsRunningZeroPid) {
  write_pid_file(0);
  EXPECT_FALSE(daemon_->IsRunning());
}

// --- Multiple constructions ---

TEST_F(DaemonManagerTest, MultipleInstancesSharePidFile) {
  write_pid_file(getpid());

  auto daemon2 = std::make_unique<DaemonManager>(logger_);
  EXPECT_EQ(daemon_->GetPid(), daemon2->GetPid());
  EXPECT_TRUE(daemon2->IsRunning());
}
