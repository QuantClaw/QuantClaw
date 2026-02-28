// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "quantclaw/platform/process.hpp"
#include "quantclaw/platform/ipc.hpp"
#include "quantclaw/platform/service.hpp"
#include <filesystem>
#include <thread>

using namespace quantclaw::platform;

// --- Process tests ---

TEST(PlatformProcess, HomeDirNotEmpty) {
  std::string home = home_directory();
  EXPECT_FALSE(home.empty());
  EXPECT_TRUE(std::filesystem::exists(home));
}

TEST(PlatformProcess, ExecutablePathNotEmpty) {
  std::string exe = executable_path();
  EXPECT_FALSE(exe.empty());
}

TEST(PlatformProcess, ExecCaptureEcho) {
  auto result = exec_capture("echo hello", 5);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.output.find("hello"), std::string::npos);
}

TEST(PlatformProcess, ExecCaptureFail) {
  auto result = exec_capture("false", 5);
  EXPECT_NE(result.exit_code, 0);
}

TEST(PlatformProcess, SpawnAndWait) {
  // Spawn a short-lived process
  std::vector<std::string> args = {"sleep", "0.1"};
  auto pid = spawn_process(args);
  ASSERT_NE(pid, kInvalidPid);
  EXPECT_TRUE(is_process_alive(pid));

  // Wait for it to finish
  int exit_code = wait_process(pid, 5000);
  EXPECT_EQ(exit_code, 0);
  EXPECT_FALSE(is_process_alive(pid));
}

TEST(PlatformProcess, SpawnInvalidBinary) {
  std::vector<std::string> args = {"/nonexistent/binary_xyz_123"};
  auto pid = spawn_process(args);
  // On Unix, fork succeeds but exec fails — child exits with 127
  if (pid != kInvalidPid) {
    int exit_code = wait_process(pid, 2000);
    EXPECT_EQ(exit_code, 127);
  }
}

TEST(PlatformProcess, TerminateProcess) {
  std::vector<std::string> args = {"sleep", "60"};
  auto pid = spawn_process(args);
  ASSERT_NE(pid, kInvalidPid);
  EXPECT_TRUE(is_process_alive(pid));

  terminate_process(pid);
  int exit_code = wait_process(pid, 5000);
  // SIGTERM results in 128+15=143 on Unix
  EXPECT_NE(exit_code, -1);
  EXPECT_FALSE(is_process_alive(pid));
}

TEST(PlatformProcess, KillProcess) {
  std::vector<std::string> args = {"sleep", "60"};
  auto pid = spawn_process(args);
  ASSERT_NE(pid, kInvalidPid);

  kill_process(pid);
  int exit_code = wait_process(pid, 5000);
  EXPECT_NE(exit_code, -1);
}

TEST(PlatformProcess, SpawnWithEnv) {
  std::vector<std::string> args = {"env"};
  std::vector<std::string> env = {"TEST_PLATFORM_VAR=hello123"};
  auto pid = spawn_process(args, env);
  ASSERT_NE(pid, kInvalidPid);
  wait_process(pid, 5000);
}

TEST(PlatformProcess, WaitNonBlockingNotExited) {
  std::vector<std::string> args = {"sleep", "60"};
  auto pid = spawn_process(args);
  ASSERT_NE(pid, kInvalidPid);

  // Non-blocking wait should return -1 (not yet exited)
  int exit_code = wait_process(pid, 0);
  EXPECT_EQ(exit_code, -1);

  terminate_process(pid);
  wait_process(pid, 5000);
}

// --- IPC tests ---

TEST(PlatformIpc, ServerListenAndCleanup) {
  std::string path = "/tmp/quantclaw_test_ipc.sock";
  IpcServer::cleanup(path);

  IpcServer server(path);
  EXPECT_TRUE(server.listen());
  EXPECT_EQ(server.path(), path);
  server.close();
  IpcServer::cleanup(path);
}

TEST(PlatformIpc, ClientServerRoundtrip) {
  std::string path = "/tmp/quantclaw_test_ipc_rt.sock";
  IpcServer::cleanup(path);

  IpcServer server(path);
  ASSERT_TRUE(server.listen());

  // Spawn a client thread
  std::thread client_thread([&path]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    IpcClient client(path);
    ASSERT_TRUE(client.connect());

    const char* msg = "hello\n";
    int written = ipc_write(client.handle(), msg, 6);
    EXPECT_EQ(written, 6);

    client.close();
  });

  auto conn = server.accept(5000);
  ASSERT_NE(conn, kInvalidIpc);

  // Read the message
  auto line = ipc_read_line(conn, 3000);
  EXPECT_EQ(line, "hello");

  ipc_close(conn);
  server.close();
  IpcServer::cleanup(path);
  client_thread.join();
}

TEST(PlatformIpc, AcceptTimeout) {
  std::string path = "/tmp/quantclaw_test_ipc_timeout.sock";
  IpcServer::cleanup(path);

  IpcServer server(path);
  ASSERT_TRUE(server.listen());

  // No client connects, should timeout
  auto conn = server.accept(100);
  EXPECT_EQ(conn, kInvalidIpc);

  server.close();
  IpcServer::cleanup(path);
}

TEST(PlatformIpc, SetPermissions) {
  std::string path = "/tmp/quantclaw_test_ipc_perms.sock";
  IpcServer::cleanup(path);

  IpcServer server(path);
  ASSERT_TRUE(server.listen());
  ipc_set_permissions(path, 0600);

  server.close();
  IpcServer::cleanup(path);
}

// --- ServiceManager tests ---

TEST(PlatformService, ConstructAndQuery) {
  auto logger = spdlog::default_logger();
  ServiceManager svc(logger);

  // Should not crash, PID should be -1 when not running
  EXPECT_EQ(svc.get_pid(), -1);
}

TEST(PlatformService, WritePidFile) {
  auto logger = spdlog::default_logger();
  ServiceManager svc(logger);

  svc.write_pid(12345);
  EXPECT_EQ(svc.get_pid(), 12345);
  svc.remove_pid();
  EXPECT_EQ(svc.get_pid(), -1);
}
