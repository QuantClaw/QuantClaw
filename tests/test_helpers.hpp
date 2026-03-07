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
#include <winsock2.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace quantclaw::test {

// Bind to port 0 and let the OS assign a free port.
// A process-wide mutex + allocated-port set prevents the same port being
// returned to two callers in the same process when CTest runs tests in
// parallel (the brief window between close() and server bind() is enough
// for a race under --parallel on a loaded CI runner).
inline int FindFreePort() {
  static std::mutex port_mutex;
  static std::set<int> allocated_ports;

  std::lock_guard<std::mutex> lock(port_mutex);

  for (int attempt = 0; attempt < 100; ++attempt) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      continue;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
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

// Create a unique test directory per process to avoid conflicts
// when CTest runs tests in parallel (gtest_discover_tests).
inline std::filesystem::path MakeTestDir(const std::string& base_name) {
#ifdef _WIN32
    int pid = _getpid();
#else
    int pid = static_cast<int>(getpid());
#endif
    auto path = std::filesystem::temp_directory_path()
                / (base_name + "_" + std::to_string(pid));
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace quantclaw::test
