// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <memory>

#include "quantclaw/platform/process.hpp"
#include "quantclaw/security/sandbox.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

#ifdef __linux__
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
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
  EXPECT_TRUE(
      quantclaw::Sandbox::ValidateFilePath("/tmp/sub/dir/file.txt", "/tmp"));
  EXPECT_FALSE(
      quantclaw::Sandbox::ValidateFilePath("../../etc/passwd", "/tmp"));
  // Absolute path outside workspace must be rejected.
  EXPECT_FALSE(quantclaw::Sandbox::ValidateFilePath("/etc/passwd", "/tmp"));
#ifdef _WIN32
  EXPECT_FALSE(quantclaw::Sandbox::ValidateFilePath(
      "C:\\Windows\\win.ini", "C:\\Users\\test\\workspace"));
  EXPECT_TRUE(quantclaw::Sandbox::ValidateFilePath(
      "C:\\Users\\test\\workspace\\file.txt", "C:\\Users\\test\\workspace"));
#endif
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
  // ApplyResourceLimits is now intentionally a no-op (resource limits are
  // applied inside exec_capture on the child process). Just verify it
  // doesn't throw.
  EXPECT_NO_THROW(quantclaw::Sandbox::ApplyResourceLimits());
}

#ifdef __linux__
TEST_F(SandboxTest, ResourceLimitsAppliedInExecCapture) {
  // Verify that resource limits are applied in the child spawned by
  // exec_capture, not on the host process.
  auto result = quantclaw::platform::exec_capture("ulimit -t", 5);
  // The child should see the CPU soft limit (30 seconds).
  EXPECT_EQ(result.exit_code, 0);
  // Trim trailing whitespace.
  std::string out = result.output;
  while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
    out.pop_back();
  EXPECT_EQ(out, "30");
}
#endif
