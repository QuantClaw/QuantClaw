// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

#include "quantclaw/platform/ipc.hpp"

#include <cerrno>
#include <cstring>
#include <chrono>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>

namespace quantclaw::platform {

// --- IpcServer ---

IpcServer::IpcServer(const std::string& path) : path_(path) {}

IpcServer::~IpcServer() { close(); }

bool IpcServer::listen() {
  listen_handle_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_handle_ < 0) return false;

  // Remove stale socket
  ::unlink(path_.c_str());

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(listen_handle_, reinterpret_cast<struct sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    ::close(listen_handle_);
    listen_handle_ = kInvalidIpc;
    return false;
  }

  if (::listen(listen_handle_, 1) < 0) {
    ::close(listen_handle_);
    listen_handle_ = kInvalidIpc;
    return false;
  }

  return true;
}

IpcHandle IpcServer::accept(int timeout_ms) {
  if (listen_handle_ < 0) return kInvalidIpc;

  if (timeout_ms >= 0) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listen_handle_, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(listen_handle_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) return kInvalidIpc;
  }

  int fd = ::accept(listen_handle_, nullptr, nullptr);
  return fd;
}

void IpcServer::close() {
  if (listen_handle_ >= 0) {
    ::close(listen_handle_);
    listen_handle_ = kInvalidIpc;
  }
}

void IpcServer::cleanup(const std::string& path) {
  ::unlink(path.c_str());
}

// --- IpcClient ---

IpcClient::IpcClient(const std::string& path) : path_(path) {}

IpcClient::~IpcClient() { close(); }

bool IpcClient::connect() {
  handle_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (handle_ < 0) return false;

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(handle_, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr)) < 0) {
    ::close(handle_);
    handle_ = kInvalidIpc;
    return false;
  }
  return true;
}

void IpcClient::close() {
  if (handle_ >= 0) {
    ::close(handle_);
    handle_ = kInvalidIpc;
  }
}

// --- Free functions ---

int ipc_write(IpcHandle h, const void* data, int len) {
  return static_cast<int>(::write(h, data, len));
}

int ipc_read(IpcHandle h, void* buf, int len) {
  return static_cast<int>(::read(h, buf, len));
}

std::string ipc_read_line(IpcHandle h, int timeout_ms) {
  std::string line;
  line.reserve(4096);
  constexpr size_t kMaxSize = 16 * 1024 * 1024;

  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(h, &fds);

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) break;

    struct timeval tv;
    tv.tv_sec = remaining.count() / 1000;
    tv.tv_usec = (remaining.count() % 1000) * 1000;

    int ret = select(h + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) break;

    char c;
    ssize_t n = ::read(h, &c, 1);
    if (n <= 0) break;
    if (c == '\n') return line;
    line += c;
    if (line.size() > kMaxSize) break;
  }
  return line;
}

void ipc_close(IpcHandle h) {
  if (h >= 0) ::close(h);
}

void ipc_set_permissions(const std::string& path, int mode) {
  chmod(path.c_str(), static_cast<mode_t>(mode));
}

}  // namespace quantclaw::platform

#endif  // !_WIN32
