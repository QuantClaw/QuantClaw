// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/cli/agent_commands.hpp"
#include "quantclaw/cli/cli_manager.hpp"
#include "quantclaw/cli/gateway_commands.hpp"
#include "quantclaw/cli/onboard_commands.hpp"
#include "quantclaw/cli/session_commands.hpp"

#include <gtest/gtest.h>

using namespace quantclaw::cli;

// Helper: convert vector<string> to argc/argv suitable for CLIManager::run
struct ArgHelper {
  std::vector<std::string> storage;
  std::vector<char*> ptrs;

  ArgHelper(std::initializer_list<std::string> args) : storage(args) {
    for (auto& s : storage) {
      ptrs.push_back(s.data());
    }
  }

  int argc() {
    return static_cast<int>(ptrs.size());
  }
  char** argv() {
    return ptrs.data();
  }
};

// Capture stdout during a callable
#ifdef _WIN32
#include <fcntl.h>

#include <io.h>
static std::string capture_stdout(std::function<void()> fn) {
  fflush(stdout);
  int pipefd[2];
  if (_pipe(pipefd, 65536, _O_BINARY) == -1) {
    return "";
  }
  int saved_stdout = _dup(_fileno(stdout));
  _dup2(pipefd[1], _fileno(stdout));
  _close(pipefd[1]);

  fn();
  fflush(stdout);

  _dup2(saved_stdout, _fileno(stdout));
  _close(saved_stdout);

  std::string result;
  char buf[1024];
  int n;
  while ((n = _read(pipefd[0], buf, sizeof(buf))) > 0) {
    result.append(buf, n);
  }
  _close(pipefd[0]);
  return result;
}

// Capture stderr during a callable
static std::string capture_stderr(std::function<void()> fn) {
  fflush(stderr);
  int pipefd[2];
  if (_pipe(pipefd, 65536, _O_BINARY) == -1) {
    return "";
  }
  int saved_stderr = _dup(_fileno(stderr));
  _dup2(pipefd[1], _fileno(stderr));
  _close(pipefd[1]);

  fn();
  fflush(stderr);

  _dup2(saved_stderr, _fileno(stderr));
  _close(saved_stderr);

  std::string result;
  char buf[1024];
  int n;
  while ((n = _read(pipefd[0], buf, sizeof(buf))) > 0) {
    result.append(buf, n);
  }
  _close(pipefd[0]);
  return result;
}
#else
#include <unistd.h>
static std::string capture_stdout(std::function<void()> fn) {
  fflush(stdout);
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return "";
  }
  int saved_stdout = dup(STDOUT_FILENO);
  dup2(pipefd[1], STDOUT_FILENO);
  close(pipefd[1]);

  fn();
  fflush(stdout);

  dup2(saved_stdout, STDOUT_FILENO);
  close(saved_stdout);

  std::string result;
  char buf[1024];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
    result.append(buf, n);
  }
  close(pipefd[0]);
  return result;
}

// Capture stderr during a callable
static std::string capture_stderr(std::function<void()> fn) {
  fflush(stderr);
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return "";
  }
  int saved_stderr = dup(STDERR_FILENO);
  dup2(pipefd[1], STDERR_FILENO);
  close(pipefd[1]);

  fn();
  fflush(stderr);

  dup2(saved_stderr, STDERR_FILENO);
  close(saved_stderr);

  std::string result;
  char buf[1024];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
    result.append(buf, n);
  }
  close(pipefd[0]);
  return result;
}
#endif

// ========== CLIManager tests ==========

class CLIManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cli_ = std::make_unique<CLIManager>();
    handler_called_ = false;
    handler_argc_ = 0;

    // Register a test command
    cli_->AddCommand(
        {"test", "A test command", {"t"}, [this](int argc, char** argv) -> int {
           handler_called_ = true;
           handler_argc_ = argc;
           return 0;
         }});
  }

  std::unique_ptr<CLIManager> cli_;
  bool handler_called_;
  int handler_argc_;
};

TEST_F(CLIManagerTest, VersionFlag) {
  ArgHelper args{"quantclaw", "--version"};
  auto output = capture_stdout([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 0);
  });
  EXPECT_NE(output.find("quantclaw"), std::string::npos);
}

TEST_F(CLIManagerTest, VersionShortFlag) {
  ArgHelper args{"quantclaw", "-v"};
  auto output = capture_stdout([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 0);
  });
  EXPECT_NE(output.find("quantclaw"), std::string::npos);
}

TEST_F(CLIManagerTest, HelpFlag) {
  ArgHelper args{"quantclaw", "--help"};
  auto output = capture_stdout([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 0);
  });
  EXPECT_NE(output.find("Usage:"), std::string::npos);
  EXPECT_NE(output.find("Commands:"), std::string::npos);
}

TEST_F(CLIManagerTest, HelpShortFlag) {
  ArgHelper args{"quantclaw", "-h"};
  auto output = capture_stdout([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 0);
  });
  EXPECT_NE(output.find("Usage:"), std::string::npos);
}

TEST_F(CLIManagerTest, NoArgsShowsHelp) {
  ArgHelper args{"quantclaw"};
  auto output = capture_stdout([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(output.find("Usage:"), std::string::npos);
}

TEST_F(CLIManagerTest, UnknownCommandReturnsError) {
  ArgHelper args{"quantclaw", "nonexistent"};
  auto err = capture_stderr([&]() {
    int ret = cli_->Run(args.argc(), args.argv());
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("Unknown command"), std::string::npos);
}

TEST_F(CLIManagerTest, CommandDispatchByName) {
  ArgHelper args{"quantclaw", "test"};
  cli_->Run(args.argc(), args.argv());
  EXPECT_TRUE(handler_called_);
}

TEST_F(CLIManagerTest, CommandDispatchByAlias) {
  ArgHelper args{"quantclaw", "t"};
  cli_->Run(args.argc(), args.argv());
  EXPECT_TRUE(handler_called_);
}

TEST_F(CLIManagerTest, CommandReceivesSubArgs) {
  ArgHelper args{"quantclaw", "test", "--foo", "bar"};
  cli_->Run(args.argc(), args.argv());
  EXPECT_TRUE(handler_called_);
  // handler gets argc-1 (argv[0]="test", argv[1]="--foo", argv[2]="bar")
  EXPECT_EQ(handler_argc_, 3);
}

TEST_F(CLIManagerTest, MultipleCommands) {
  bool second_called = false;
  cli_->AddCommand(
      {"other", "Another command", {}, [&second_called](int, char**) -> int {
         second_called = true;
         return 42;
       }});

  ArgHelper args{"quantclaw", "other"};
  int ret = cli_->Run(args.argc(), args.argv());
  EXPECT_TRUE(second_called);
  EXPECT_EQ(ret, 42);
  EXPECT_FALSE(handler_called_);
}

// ========== AgentCommands tests ==========

class AgentCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_agent_cmd", null_sink);
    agent_cmds_ = std::make_unique<AgentCommands>(logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<AgentCommands> agent_cmds_;
};

TEST_F(AgentCommandsTest, RequestNoMessageReturnsError) {
  auto err = capture_stderr([&]() {
    int ret = agent_cmds_->RequestCommand({});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("Usage:"), std::string::npos);
}

TEST_F(AgentCommandsTest, RequestOnlyDashMFlagNoValue) {
  // -m with no following argument → treated as no message
  auto err = capture_stderr([&]() {
    int ret = agent_cmds_->RequestCommand({"-m"});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("Usage:"), std::string::npos);
}

// ========== SessionCommands tests ==========

class SessionCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_session_cmd", null_sink);
    session_cmds_ = std::make_unique<SessionCommands>(logger_);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<SessionCommands> session_cmds_;
};

TEST_F(SessionCommandsTest, HistoryNoSessionKeyReturnsError) {
  auto err = capture_stderr([&]() {
    int ret = session_cmds_->HistoryCommand({});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("session key required"), std::string::npos);
}

TEST_F(SessionCommandsTest, HistoryOnlyFlagsNoKey) {
  // Only flags, no positional session key
  auto err = capture_stderr([&]() {
    int ret = session_cmds_->HistoryCommand({"--json", "--limit", "10"});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("session key required"), std::string::npos);
}

TEST_F(SessionCommandsTest, DeleteNoSessionKeyReturnsError) {
  auto err = capture_stderr([&]() {
    int ret = session_cmds_->DeleteCommand({});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("session key required"), std::string::npos);
}

TEST_F(SessionCommandsTest, ResetNoSessionKeyReturnsError) {
  auto err = capture_stderr([&]() {
    int ret = session_cmds_->ResetCommand({});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("session key required"), std::string::npos);
}

// ========== GatewayCommands construction ==========

class GatewayCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_gw_cmd", null_sink);
  }

  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(GatewayCommandsTest, Construction) {
  EXPECT_NO_THROW({ GatewayCommands gw(logger_); });
}

TEST_F(GatewayCommandsTest, CallCommandWithoutMethodReturnsUsageError) {
  GatewayCommands gw(logger_);
  auto err = capture_stderr([&]() {
    int ret = gw.CallCommand({});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("Usage:"), std::string::npos);
}

TEST_F(GatewayCommandsTest, CallCommandInvalidJsonReturnsError) {
  GatewayCommands gw(logger_);
  auto err = capture_stderr([&]() {
    int ret = gw.CallCommand({"gateway.status", "{invalid-json"});
    EXPECT_EQ(ret, 1);
  });
  EXPECT_NE(err.find("Invalid JSON params"), std::string::npos);
}

TEST_F(GatewayCommandsTest, StatusWhenGatewayUnavailableReturnsError) {
  GatewayCommands gw(logger_);
  gw.SetGatewayUrl("ws://127.0.0.1:1");
  int ret = gw.StatusCommand({"--json"});
  EXPECT_EQ(ret, 1);
}

class EnvVarGuard {
 public:
  EnvVarGuard(const std::string& key, const std::string& value) : key_(key) {
#ifdef _WIN32
    char* old = nullptr;
    size_t len = 0;
    _dupenv_s(&old, &len, key.c_str());
    if (old) {
      had_old_ = true;
      old_value_ = old;
      free(old);
    }
    _putenv_s(key.c_str(), value.c_str());
#else
    const char* old = std::getenv(key.c_str());
    if (old) {
      had_old_ = true;
      old_value_ = old;
    }
    setenv(key.c_str(), value.c_str(), 1);
#endif
  }

  ~EnvVarGuard() {
#ifdef _WIN32
    if (had_old_) {
      _putenv_s(key_.c_str(), old_value_.c_str());
    } else {
      _putenv_s(key_.c_str(), "");
    }
#else
    if (had_old_) {
      setenv(key_.c_str(), old_value_.c_str(), 1);
    } else {
      unsetenv(key_.c_str());
    }
#endif
  }

 private:
  std::string key_;
  bool had_old_ = false;
  std::string old_value_;
};

class OnboardCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test_onboard_cmd", null_sink);

    auto unique = std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch()
                                     .count());
    temp_home_ = std::filesystem::temp_directory_path() /
                 ("quantclaw_onboard_test_" + unique);
    std::filesystem::create_directories(temp_home_);
    home_guard_ = std::make_unique<EnvVarGuard>("HOME", temp_home_.string());

    onboard_ = std::make_unique<OnboardCommands>(logger_);
  }

  void TearDown() override {
    onboard_.reset();
    home_guard_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_home_, ec);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<OnboardCommands> onboard_;
  std::filesystem::path temp_home_;
  std::unique_ptr<EnvVarGuard> home_guard_;
};

TEST_F(OnboardCommandsTest, QuickSetupCreatesConfigAndWorkspaceFiles) {
  int ret = onboard_->QuickSetupCommand({});
  EXPECT_EQ(ret, 0);

  auto config = temp_home_ / ".quantclaw" / "quantclaw.json";
  auto ws = temp_home_ / ".quantclaw" / "agents" / "main" / "workspace";

  EXPECT_TRUE(std::filesystem::exists(config));
  EXPECT_TRUE(std::filesystem::exists(ws / "SOUL.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "MEMORY.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "SKILL.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "IDENTITY.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "HEARTBEAT.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "USER.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "AGENTS.md"));
  EXPECT_TRUE(std::filesystem::exists(ws / "TOOLS.md"));
}

TEST_F(OnboardCommandsTest, OnboardSkipDaemonWithDefaultAnswers) {
  std::istringstream scripted_input("\n\n\n");
  auto* old_buf = std::cin.rdbuf(scripted_input.rdbuf());

  int ret = onboard_->OnboardCommand({"--skip-daemon"});

  std::cin.rdbuf(old_buf);
  EXPECT_EQ(ret, 0);

  auto config = temp_home_ / ".quantclaw" / "quantclaw.json";
  EXPECT_TRUE(std::filesystem::exists(config));
}

// Note: status_command, start_command etc. involve gateway connections
// and are tested in the E2E test suite to avoid slow timeouts here.
