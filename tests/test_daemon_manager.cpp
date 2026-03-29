// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

#ifdef _WIN32
#include <process.h>
#define test_getpid() _getpid()
#define test_setenv(name, value) _putenv_s(name, value)
#define test_unsetenv(name) _putenv_s(name, "")
#else
#include <unistd.h>
#define test_getpid() getpid()
#define test_setenv(name, value) setenv(name, value, 1)
#define test_unsetenv(name) unsetenv(name)
#endif

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

    // Save original values (empty string means the variable was unset)
    auto get_or_empty = [](const char* name) -> std::string {
      const char* v = std::getenv(name);
      return v ? v : "";
    };
#ifdef _WIN32
    orig_userprofile_ = get_or_empty("USERPROFILE");
    orig_home_ = get_or_empty("HOME");
    orig_path_ = get_or_empty("PATH");
    test_setenv("USERPROFILE", test_home_.string().c_str());
    test_setenv("HOME", test_home_.string().c_str());
#else
    orig_home_ = get_or_empty("HOME");
    orig_path_ = get_or_empty("PATH");
    test_setenv("HOME", test_home_.string().c_str());
#endif

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_daemon", null_sink);

    daemon_ = std::make_unique<DaemonManager>(logger_);
  }

  void TearDown() override {
    daemon_.reset();
    // Restore each variable independently
#ifdef _WIN32
    if (!orig_userprofile_.empty()) {
      test_setenv("USERPROFILE", orig_userprofile_.c_str());
    } else {
      test_unsetenv("USERPROFILE");
    }
    if (!orig_home_.empty()) {
      test_setenv("HOME", orig_home_.c_str());
    } else {
      test_unsetenv("HOME");
    }
#else
    if (!orig_home_.empty()) {
      test_setenv("HOME", orig_home_.c_str());
    } else {
      test_unsetenv("HOME");
    }
#endif
    if (!orig_path_.empty()) {
      test_setenv("PATH", orig_path_.c_str());
    } else {
      test_unsetenv("PATH");
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

#ifndef _WIN32
  std::filesystem::path fake_service_state_path(const std::string& name) const {
    return test_home_ / (name + ".state");
  }

  std::string read_file(const std::filesystem::path& path) const {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  }

  std::filesystem::path
  install_fake_service_command(const std::string& name,
                               bool service_loaded = false) {
    auto bin_dir = test_home_ / "bin";
    std::filesystem::create_directories(bin_dir);

    auto log_path = test_home_ / (name + ".log");
    auto script_path = bin_dir / name;
    std::ofstream script(script_path);
    script << "#!/bin/sh\n";
    script << "echo \"$@\" >> \"" << log_path.string() << "\"\n";
#ifdef __APPLE__
    auto state_path = fake_service_state_path(name);
    script << "STATE_FILE=\"" << state_path.string() << "\"\n";
    script << "if [ \"$1\" = \"print\" ]; then\n";
    script << "  if [ -f \"$STATE_FILE\" ]; then\n";
    script << "    cat \"$STATE_FILE\"\n";
    script << "    exit 0\n";
    script << "  fi\n";
    script << "  exit 1\n";
    script << "fi\n";
    script << "if [ \"$1\" = \"bootstrap\" ]; then\n";
    script << "  printf 'pid = 4242\\n' > \"$STATE_FILE\"\n";
    script << "  exit 0\n";
    script << "fi\n";
    script << "if [ \"$1\" = \"kickstart\" ]; then\n";
    script << "  [ -f \"$STATE_FILE\" ]\n";
    script << "  exit $?\n";
    script << "fi\n";
    script << "if [ \"$1\" = \"bootout\" ]; then\n";
    script << "  rm -f \"$STATE_FILE\"\n";
    script << "  exit 0\n";
    script << "fi\n";
#else
    script << "if [ \"$2\" = \"show\" ]; then\n";
    script << "  echo \"4242\"\n";
    script << "  exit 0\n";
    script << "fi\n";
    script << "if [ \"$2\" = \"is-active\" ]; then\n";
    script << "  exit 0\n";
    script << "fi\n";
#endif
    script << "exit 0\n";
    script.close();
    std::filesystem::permissions(script_path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::group_exec,
                                 std::filesystem::perm_options::replace);

#ifdef __APPLE__
    if (service_loaded) {
      std::ofstream state(state_path);
      state << "pid = 4242\n";
    }
#endif

    std::string new_path = bin_dir.string();
    if (!orig_path_.empty()) {
      new_path += ":" + orig_path_;
    }
    test_setenv("PATH", new_path.c_str());
    return log_path;
  }
#endif

  std::filesystem::path expected_service_path() const {
#ifdef _WIN32
    return test_home_ / ".quantclaw" / "gateway.service.json";
#elif defined(__APPLE__)
    return test_home_ / "Library" / "LaunchAgents" /
           "com.quantclaw.gateway.plist";
#else
    return test_home_ / ".config" / "systemd" / "user" /
           "quantclaw-gateway.service";
#endif
  }

  std::filesystem::path test_home_;
  std::string orig_home_;
  std::string orig_path_;
#ifdef _WIN32
  std::string orig_userprofile_;
#endif
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
  write_pid_file(test_getpid());
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
  write_pid_file(test_getpid());

  auto daemon2 = std::make_unique<DaemonManager>(logger_);
  EXPECT_EQ(daemon_->GetPid(), daemon2->GetPid());
  EXPECT_TRUE(daemon2->IsRunning());
}

#ifndef _WIN32
TEST_F(DaemonManagerTest, InstallWritesPlatformServiceDefinition) {
#ifdef __APPLE__
  install_fake_service_command("launchctl");
#else
  install_fake_service_command("systemctl");
#endif

  EXPECT_EQ(daemon_->Install(19001), 0);

  auto svc_path = expected_service_path();
  ASSERT_TRUE(std::filesystem::exists(svc_path));

  std::ifstream in(svc_path);
  std::string contents((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());

#ifdef __APPLE__
  EXPECT_NE(contents.find("com.quantclaw.gateway"), std::string::npos);
  EXPECT_NE(contents.find("<key>ProgramArguments</key>"), std::string::npos);
  EXPECT_NE(contents.find("<string>gateway</string>"), std::string::npos);
  EXPECT_NE(contents.find("<string>19001</string>"), std::string::npos);
  EXPECT_NE(contents.find("<key>StandardOutPath</key>"), std::string::npos);
#else
  EXPECT_NE(contents.find("[Unit]"), std::string::npos);
  EXPECT_NE(contents.find("ExecStart="), std::string::npos);
  EXPECT_NE(contents.find("gateway run --port 19001"), std::string::npos);
#endif
}

TEST_F(DaemonManagerTest, InstallUsesDefaultGatewayPort) {
#ifdef __APPLE__
  install_fake_service_command("launchctl");
#else
  install_fake_service_command("systemctl");
#endif

  ASSERT_EQ(daemon_->Install(), 0);

  std::ifstream in(expected_service_path());
  ASSERT_TRUE(in.good());
  std::string contents((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());

#ifdef __APPLE__
  EXPECT_NE(contents.find("<string>18800</string>"), std::string::npos);
  EXPECT_EQ(contents.find("<string>18789</string>"), std::string::npos);
#else
  EXPECT_NE(contents.find("gateway run --port 18800"), std::string::npos);
  EXPECT_EQ(contents.find("gateway run --port 18789"), std::string::npos);
#endif
}

TEST_F(DaemonManagerTest, IsRunningFallsBackToServiceManagerWithoutPidFile) {
#ifdef __APPLE__
  install_fake_service_command("launchctl", true);
#else
  install_fake_service_command("systemctl");
#endif

  EXPECT_TRUE(daemon_->IsRunning());
  EXPECT_EQ(daemon_->GetPid(), 4242);
}

#ifdef __APPLE__
TEST_F(DaemonManagerTest, GetPidRejectsNegativePidFromLaunchctl) {
  install_fake_service_command("launchctl", true);
  std::ofstream state(fake_service_state_path("launchctl"));
  state << "pid = -42\n";
  state.close();

  EXPECT_EQ(daemon_->GetPid(), -1);
}

TEST_F(DaemonManagerTest, GetPidRejectsOverflowPidFromLaunchctl) {
  install_fake_service_command("launchctl", true);
  std::ofstream state(fake_service_state_path("launchctl"));
  state << "pid = 999999999999999999999\n";
  state.close();

  EXPECT_EQ(daemon_->GetPid(), -1);
}

TEST_F(DaemonManagerTest, StartCallsLaunchctlBootstrapAndKickstart) {
  auto log_path = install_fake_service_command("launchctl");
  ASSERT_EQ(daemon_->Install(19001), 0);

  EXPECT_EQ(daemon_->Start(), 0);

  auto log = read_file(log_path);
  EXPECT_NE(log.find("print gui/"), std::string::npos);
  EXPECT_NE(log.find("bootstrap gui/"), std::string::npos);
  EXPECT_NE(log.find("kickstart -k gui/"), std::string::npos);
}

TEST_F(DaemonManagerTest, StopCallsLaunchctlBootout) {
  auto log_path = install_fake_service_command("launchctl", true);

  EXPECT_EQ(daemon_->Stop(), 0);

  auto log = read_file(log_path);
  EXPECT_NE(log.find("bootout gui/"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(fake_service_state_path("launchctl")));
}

TEST_F(DaemonManagerTest, RestartCombinesStopAndStart) {
  auto log_path = install_fake_service_command("launchctl", true);
  ASSERT_EQ(daemon_->Install(19001), 0);

  EXPECT_EQ(daemon_->Restart(), 0);

  auto log = read_file(log_path);
  EXPECT_NE(log.find("bootout gui/"), std::string::npos);
  EXPECT_NE(log.find("bootstrap gui/"), std::string::npos);
  EXPECT_NE(log.find("kickstart -k gui/"), std::string::npos);
}

TEST_F(DaemonManagerTest, StatusCallsLaunchctlPrint) {
  auto log_path = install_fake_service_command("launchctl", true);

  EXPECT_EQ(daemon_->Status(), 0);

  auto log = read_file(log_path);
  EXPECT_NE(log.find("print gui/"), std::string::npos);
}
#endif
#endif
