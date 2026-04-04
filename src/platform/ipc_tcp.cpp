// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// TCP loopback IPC - single implementation for Linux, macOS, and Windows.
// Replaces ipc_unix.cpp (AF_UNIX) and ipc_win32.cpp (Named Pipes).
//
// The C++ parent binds to 127.0.0.1:0 (OS picks a free port), then passes
// the port to the sidecar child via QUANTCLAW_PORT. The sidecar connects
// back with net.createConnection(port, "127.0.0.1").

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include "quantclaw/platform/ipc.hpp"

#ifdef _WIN32
// ---- Windows WinSock2 ----
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET

// Auto-initialise WinSock2 at startup.
namespace {
struct WinSockInit {
  WinSockInit() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  }
  ~WinSockInit() {
    WSACleanup();
  }
} g_winsock;

inline void close_fd(socket_t s) {
  closesocket(s);
}
}  // namespace

#else
// ---- POSIX ----
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

using socket_t = int;
#define INVALID_SOCK (-1)

namespace {
// Use ::close to explicitly call the POSIX close() rather than a member
// close().
inline void close_fd(socket_t s) {
  ::close(s);
}
}  // namespace
#endif

namespace quantclaw::platform {

namespace {

bool parse_ipv4_address(const std::string& host, in_addr& out) {
#ifdef _WIN32
  return InetPtonA(AF_INET, host.c_str(), &out) == 1;
#else
  return inet_pton(AF_INET, host.c_str(), &out) == 1;
#endif
}

#ifdef _WIN32
using socket_io_len_t = int;
#else
using socket_io_len_t = size_t;
#endif

// Unified select() wrapper - waits for readability on `sock` up to `ms`.
// Returns true if data is available before the deadline.
bool wait_readable(socket_t sock, int ms) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);

  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;

#ifdef _WIN32
  // On Windows the first argument to select() is ignored.
  int ret = select(0, &fds, nullptr, nullptr, &tv);
#else
  int ret = select(static_cast<int>(sock) + 1, &fds, nullptr, nullptr, &tv);
#endif
  return ret > 0;
}

}  // namespace

// IpcServer

IpcServer::IpcServer(const std::string& path) : path_(path) {}

IpcServer::~IpcServer() {
  close();
}

bool IpcServer::listen() {
  socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCK)
    return false;

  // Permit rapid re-bind in tests.
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  if (!parse_ipv4_address("127.0.0.1", addr.sin_addr)) {
    close_fd(sock);
    return false;
  }
  addr.sin_port = 0;  // OS picks a free port.

  if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    close_fd(sock);
    return false;
  }

  if (::listen(sock, 1) < 0) {
    close_fd(sock);
    return false;
  }

  // Retrieve the actual port assigned by the OS.
  struct sockaddr_in bound {};
#ifdef _WIN32
  int bound_len = sizeof(bound);
#else
  socklen_t bound_len = sizeof(bound);
#endif
  getsockname(sock, reinterpret_cast<struct sockaddr*>(&bound), &bound_len);
  port_ = ntohs(bound.sin_port);

  listen_handle_ = static_cast<IpcHandle>(sock);
  return true;
}

IpcHandle IpcServer::accept(int timeout_ms) {
  if (listen_handle_ == kInvalidIpc)
    return kInvalidIpc;
  socket_t ls = static_cast<socket_t>(listen_handle_);

  if (timeout_ms >= 0 && !wait_readable(ls, timeout_ms)) {
    return kInvalidIpc;
  }

  socket_t conn = ::accept(ls, nullptr, nullptr);
  if (conn == INVALID_SOCK)
    return kInvalidIpc;
  return static_cast<IpcHandle>(conn);
}

void IpcServer::close() {
  if (listen_handle_ != kInvalidIpc) {
    close_fd(static_cast<socket_t>(listen_handle_));
    listen_handle_ = kInvalidIpc;
  }
}

void IpcServer::cleanup(const std::string& /*path*/) {
  // No-op: TCP has no socket file to remove.
}

// IpcClient

IpcClient::IpcClient(const std::string& host, int port)
    : host_(host), port_(port) {}

IpcClient::IpcClient(const std::string& path) {
  // Try to parse "host:port" from path for legacy callers.
  // Falls back to 127.0.0.1 with port 0 (connect will fail gracefully).
  auto colon = path.rfind(':');
  if (colon != std::string::npos) {
    host_ = path.substr(0, colon);
    try {
      port_ = std::stoi(path.substr(colon + 1));
    } catch (...) {}
  } else {
    host_ = "127.0.0.1";
    port_ = 0;
  }
}

IpcClient::~IpcClient() {
  close();
}

bool IpcClient::connect() {
  if (port_ <= 0)
    return false;

  socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCK)
    return false;

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  if (!parse_ipv4_address(host_, addr.sin_addr)) {
    close_fd(sock);
    return false;
  }
  addr.sin_port = htons(static_cast<uint16_t>(port_));

  if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
      0) {
    close_fd(sock);
    return false;
  }

  handle_ = static_cast<IpcHandle>(sock);
  return true;
}

void IpcClient::close() {
  if (handle_ != kInvalidIpc) {
    close_fd(static_cast<socket_t>(handle_));
    handle_ = kInvalidIpc;
  }
}

// Free functions

int ipc_write(IpcHandle h, const void* data, int len) {
  if (len < 0)
    return -1;
  return static_cast<int>(send(static_cast<socket_t>(h),
                               static_cast<const char*>(data),
                               static_cast<socket_io_len_t>(len), 0));
}

int ipc_read(IpcHandle h, void* buf, int len) {
  if (len < 0)
    return -1;
  return static_cast<int>(recv(static_cast<socket_t>(h),
                               static_cast<char*>(buf),
                               static_cast<socket_io_len_t>(len), 0));
}

std::string ipc_read_line(IpcHandle h, int timeout_ms) {
  std::string line;
  line.reserve(4096);
  constexpr size_t kMaxSize = 16 * 1024 * 1024;

  socket_t sock = static_cast<socket_t>(h);
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0)
      break;

    if (!wait_readable(sock, static_cast<int>(remaining.count())))
      break;

    char c;
    int n = static_cast<int>(recv(sock, &c, 1, 0));
    if (n <= 0)
      break;
    if (c == '\n')
      return line;
    line += c;
    if (line.size() > kMaxSize)
      break;
  }
  return line;
}

void ipc_close(IpcHandle h) {
  if (h != kInvalidIpc) {
    close_fd(static_cast<socket_t>(h));
  }
}

void ipc_set_permissions(const std::string& /*path*/, int /*mode*/) {
  // No-op: TCP has no socket file with Unix permissions.
}

}  // namespace quantclaw::platform
