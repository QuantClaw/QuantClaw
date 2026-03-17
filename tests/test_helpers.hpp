// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

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
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

namespace quantclaw::test {

namespace detail {

inline void close_socket(socket_t s) {
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

// Process-wide list of (port, socket) pairs held by FindFreePort().
// Guarded by held_mutex().
inline std::mutex& held_mutex() {
  static std::mutex mu;
  return mu;
}
inline std::vector<std::pair<int, socket_t>>& held_sockets() {
  static std::vector<std::pair<int, socket_t>> v;
  return v;
}

}  // namespace detail

/// Allocates an ephemeral TCP port that is safe for parallel ctest use.
///
/// Binds to port 0 so the OS assigns a free port, then **keeps the socket
/// open** so no other process can receive the same port from the OS.
/// Call ReleaseHeldPorts() just before server->Start() to free the socket
/// so the server can bind.  Because the probe socket was never connected
/// or listened on, closing it does not enter TIME_WAIT and the port is
/// immediately available.
///
/// @return A free port number in [1024, 65535], or 0 on failure.
inline int FindFreePort() {
  std::lock_guard<std::mutex> lock(detail::held_mutex());

  for (int attempt = 0; attempt < 100; ++attempt) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket)
      return 0;

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

    int port = ntohs(addr.sin_port);

    // Keep the socket bound so the OS won't hand this port to another
    // parallel test process.
    detail::held_sockets().push_back({port, sock});
    return port;
  }
  return 0;
}

/// Releases all sockets held by FindFreePort().
///
/// Call this right before server->Start() so the port becomes available
/// for the server to bind.  Since the probe sockets were only bound
/// (never connected or listened on), closing them does not enter
/// TIME_WAIT — the port is immediately reusable.
inline void ReleaseHeldPorts() {
  std::lock_guard<std::mutex> lock(detail::held_mutex());
  for (auto& [port, s] : detail::held_sockets()) {
    detail::close_socket(s);
  }
  detail::held_sockets().clear();
}

/// Releases the socket held for a specific port.
///
/// Use this when you have multiple servers to start sequentially and want to
/// release each port reservation only immediately before its server binds,
/// keeping the others reserved to prevent parallel tests from grabbing them.
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

/// Blocks until a TCP connection to localhost:port succeeds or timeout_ms
/// elapses.  Used in test fixtures to wait for a server to be fully ready
/// instead of a blind sleep_for(), which is unreliable under CI load or
/// TSan slowdown.
///
/// @return true if the server accepted a probe connection within the timeout.
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
      return true;  // Server is accepting connections
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

}  // namespace quantclaw::test
