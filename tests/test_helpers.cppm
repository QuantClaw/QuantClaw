// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "test_helpers_platform.h"

export module quantclaw.test.helpers;

import std;

export namespace quantclaw::test {

namespace detail {

inline void close_socket(socket_t s) {
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

inline std::mutex& held_mutex() {
  static std::mutex mu;
  return mu;
}
inline std::vector<std::pair<int, socket_t>>& held_sockets() {
  static std::vector<std::pair<int, socket_t>> v;
  return v;
}

}  // namespace detail

inline int FindFreePort() {
  std::lock_guard<std::mutex> lock(detail::held_mutex());

  for (int attempt = 0; attempt < 100; ++attempt) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
      // Transient resource exhaustion (EMFILE/ENFILE) — yield and retry.
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Allow the port to be reused immediately after the reservation socket
    // is closed (avoids EADDRINUSE when the server binds the same port).
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
        0) {
      detail::close_socket(sock);
      continue;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &len) <
        0) {
      detail::close_socket(sock);
      continue;
    }

    int port = static_cast<int>(ntohs(addr.sin_port));
    detail::held_sockets().push_back({port, sock});
    return port;
  }
  return 0;
}

inline void ReleaseHeldPorts() {
  std::lock_guard<std::mutex> lock(detail::held_mutex());
  for (auto& [port, s] : detail::held_sockets()) {
    detail::close_socket(s);
  }
  detail::held_sockets().clear();
}

inline void ReleaseHeldPort(int port) {
  std::lock_guard<std::mutex> lock(detail::held_mutex());
  auto& socks = detail::held_sockets();
  for (auto it = socks.begin(); it != socks.end(); ++it) {
    if (it->first == port) {
      detail::close_socket(it->second);
      socks.erase(it);
      return;
    }
  }
}

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

inline bool WaitForServerReady(int port, int timeout_ms = 5000) {
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket)
      return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    int rc =
        connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    detail::close_socket(sock);
    if (rc == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

}  // namespace quantclaw::test