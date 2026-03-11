// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <memory>

#include "quantclaw/security/sandbox.hpp"

#include <gtest/gtest.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_helpers.hpp"
#endif

class SandboxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_sandbox_test");
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::filesystem::path test_dir_;
};

TEST_F(SandboxTest, AllowedPathWithinWorkspace) {
  quantclaw::Sandbox sandbox(test_dir_, {test_dir_.string()},  // allowed
                             {},                               // denied
                             {},  // allowed commands
                             {}   // denied commands
  );

  auto file_in_workspace = test_dir_ / "SOUL.md";
  EXPECT_TRUE(sandbox.IsPathAllowed(file_in_workspace.string()));
}

TEST_F(SandboxTest, DeniedPathOutsideWorkspace) {
  quantclaw::Sandbox sandbox(test_dir_, {test_dir_.string()},  // allowed
                             {},                               // denied
                             {}, {});

  EXPECT_FALSE(sandbox.IsPathAllowed("/etc/passwd"));
}

TEST_F(SandboxTest, ExplicitDenyOverridesAllow) {
  quantclaw::Sandbox sandbox(test_dir_, {"/"},  // allow everything
                             {"/etc"},          // but deny /etc
                             {}, {});

  EXPECT_FALSE(sandbox.IsPathAllowed("/etc/passwd"));
  EXPECT_TRUE(sandbox.IsPathAllowed("/tmp/test.txt"));
}

TEST_F(SandboxTest, EmptyAllowedPathsPermitsAll) {
  quantclaw::Sandbox sandbox(
      test_dir_, {},  // no allowed paths → permit all (except denied)
      {}, {}, {});

  EXPECT_TRUE(sandbox.IsPathAllowed("/tmp/anything"));
}

TEST_F(SandboxTest, SanitizePathTraversal) {
  quantclaw::Sandbox sandbox(test_dir_, {}, {}, {}, {});

  EXPECT_THROW(sandbox.SanitizePath("../../../etc/passwd"), std::runtime_error);
}

TEST_F(SandboxTest, SanitizeNormalPath) {
  quantclaw::Sandbox sandbox(test_dir_, {}, {}, {}, {});

  auto result = sandbox.SanitizePath(test_dir_.string() + "/SOUL.md");
  EXPECT_FALSE(result.empty());
}

// --- Static validators ---

TEST_F(SandboxTest, ValidateFilePath) {
  EXPECT_TRUE(quantclaw::Sandbox::ValidateFilePath("/tmp/test.txt", "/tmp"));
  EXPECT_FALSE(
      quantclaw::Sandbox::ValidateFilePath("../../etc/passwd", "/tmp"));
}

TEST_F(SandboxTest, ValidateShellCommandSafe) {
  EXPECT_TRUE(quantclaw::Sandbox::ValidateShellCommand("ls -la"));
  EXPECT_TRUE(quantclaw::Sandbox::ValidateShellCommand("echo hello"));
}

TEST_F(SandboxTest, ValidateShellCommandDangerous) {
  EXPECT_FALSE(quantclaw::Sandbox::ValidateShellCommand("rm -rf /"));
  EXPECT_FALSE(
      quantclaw::Sandbox::ValidateShellCommand("dd if=/dev/zero of=/dev/sda"));
  EXPECT_FALSE(quantclaw::Sandbox::ValidateShellCommand("mkfs.ext4 /dev/sda"));
}

// --- Command filtering ---

TEST_F(SandboxTest, DenyCommandByPattern) {
  quantclaw::Sandbox sandbox(test_dir_, {}, {}, {}, {"rm\\s+-rf"}
                             // denied command pattern (regex)
  );

  EXPECT_FALSE(sandbox.IsCommandAllowed("rm -rf /"));
  EXPECT_TRUE(sandbox.IsCommandAllowed("ls -la"));
}

// --- Resource limits ---

TEST_F(SandboxTest, ApplyResourceLimitsDoesNotThrow) {
#ifdef __linux__
  // Run in a child process to avoid permanently restricting the test process
  pid_t pid = fork();
  ASSERT_NE(pid, -1) << "fork() failed";
  if (pid == 0) {
    quantclaw::Sandbox::ApplyResourceLimits();
    _exit(0);  // No throw
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
#else
  EXPECT_NO_THROW(quantclaw::Sandbox::ApplyResourceLimits());
#endif
}

#ifdef __linux__
TEST_F(SandboxTest, ResourceLimitsAreSet) {
  // Fork a child to test resource limits without affecting the test process.
  // Hard limits can't be raised back without root privileges.
  pid_t pid = fork();
  ASSERT_NE(pid, -1) << "fork() failed";

  if (pid == 0) {
    // Child process: apply limits and verify
    quantclaw::Sandbox::ApplyResourceLimits();

    struct rlimit cpu_limit;
    getrlimit(RLIMIT_CPU, &cpu_limit);
    if (cpu_limit.rlim_cur != 30 || cpu_limit.rlim_max != 60)
      _exit(1);

    struct rlimit fsize_limit;
    getrlimit(RLIMIT_FSIZE, &fsize_limit);
    if (fsize_limit.rlim_cur != 64u * 1024 * 1024 ||
        fsize_limit.rlim_max != 128u * 1024 * 1024)
      _exit(2);

    struct rlimit nproc_limit;
    getrlimit(RLIMIT_NPROC, &nproc_limit);
    if (nproc_limit.rlim_cur != 32 || nproc_limit.rlim_max != 64)
      _exit(3);

    _exit(0);  // All checks passed
  }

  // Parent: wait for child and check exit status
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0)
      << "Child exit code indicates resource limit mismatch";
}
#endif
