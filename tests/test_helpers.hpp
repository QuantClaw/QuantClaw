// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace quantclaw::test {

/// Allocates an ephemeral TCP port that is unique within this process.
///
/// Binds to port 0 so the OS assigns a free port, then immediately closes
/// the socket and records the port in a process-wide set so the same port
/// is never returned to a second caller. Up to 100 attempts are made to
/// find a port not already reserved by a prior call in this process.
///
/// This prevents spurious EADDRINUSE failures when CTest runs tests in
/// parallel: the brief window between close() and the server's own bind()
/// is enough for a race on a loaded CI runner without this deduplication.
///
/// @return A free port number in [1024, 65535], or 0 on failure.
inline int FindFreePort() {
  static std::mutex port_mutex;
  static std::set<int> allocated_ports;

  std::lock_guard<std::mutex> lock(port_mutex);

  for (int attempt = 0; attempt < 100; ++attempt) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
      return 0;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
        0) {
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      continue;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &len) <
        0) {
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      continue;
    }

    int port = ntohs(addr.sin_port);

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    if (allocated_ports.find(port) == allocated_ports.end()) {
      allocated_ports.insert(port);
      return port;
    }
    // Port already reserved by this process — retry for a different one.
  }
  return 0;
}

/// Creates a temporary test directory that is unique to the current process.
///
/// The directory path is formed as `<tmpdir>/<base_name>_<pid>`, which avoids
/// collisions when CTest runs multiple test binaries in parallel via
/// gtest_discover_tests. The directory is created immediately on call.
///
/// @param base_name  Human-readable prefix used in the directory name.
/// @return Absolute path to the newly created directory.
inline std::filesystem::path MakeTestDir(const std::string& base_name) {
#ifdef _WIN32
  int pid = _getpid();
#else
  int pid = static_cast<int>(getpid());
#endif
  auto path = std::filesystem::temp_directory_path() /
              (base_name + "_" + std::to_string(pid));
  std::filesystem::create_directories(path);
  return path;
}

}  // namespace quantclaw::test
