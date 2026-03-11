// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace quantclaw::platform {

// Platform-neutral IPC handle — TCP socket file descriptor on all platforms.
// On Linux/macOS: int fd. On Windows: SOCKET (uintptr_t) cast to intptr_t.
using IpcHandle = intptr_t;
constexpr IpcHandle kInvalidIpc = -1;

// IPC transport: TCP loopback (127.0.0.1) on all platforms.
// Provides a stream-oriented, bidirectional byte channel.
// Replaces the former Unix domain socket / Named Pipe implementation.

// Server side: bind to 127.0.0.1:port (0 = OS-assigned), listen, accept.
class IpcServer {
 public:
  // `path` is ignored; kept for API compatibility.
  // After listen(), use port() to get the actual bound port.
  explicit IpcServer(const std::string& path = "");
  ~IpcServer();

  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;

  // Bind and listen on 127.0.0.1:0. Returns true on success.
  bool listen();

  // Accept one client connection. Blocks up to `timeout_ms` (-1 = forever).
  // Returns a valid handle on success.
  IpcHandle accept(int timeout_ms = -1);

  // Close the listener.
  void close();

  // No-op for TCP (no socket file to remove). Kept for API compatibility.
  static void cleanup(const std::string& path);

  // Get the TCP port bound after listen().
  int port() const {
    return port_;
  }

  // Returns the path string passed to the constructor (may be empty).
  const std::string& path() const {
    return path_;
  }

 private:
  std::string path_;
  int port_ = 0;
  IpcHandle listen_handle_ = kInvalidIpc;
};

// Client side: connect to a TCP server on 127.0.0.1:port.
class IpcClient {
 public:
  // Connect to 127.0.0.1:port.
  IpcClient(const std::string& host, int port);

  // Legacy constructor: path is parsed as "host:port" or ignored.
  // Kept for API compatibility; prefer IpcClient(host, port).
  explicit IpcClient(const std::string& path);

  ~IpcClient();

  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  // Connect to server. Returns true on success.
  bool connect();

  // Get the connection handle (for read/write).
  IpcHandle handle() const {
    return handle_;
  }

  // Close the connection.
  void close();

 private:
  std::string host_;
  int port_ = 0;
  IpcHandle handle_ = kInvalidIpc;
};

// Read/write on an IPC handle.
// Returns number of bytes transferred, or -1 on error.
int ipc_write(IpcHandle h, const void* data, int len);
int ipc_read(IpcHandle h, void* buf, int len);

// Read a newline-terminated line from an IPC handle with timeout.
// Returns the line (without newline), or empty string on timeout/error.
std::string ipc_read_line(IpcHandle h, int timeout_ms);

// Close an IPC handle.
void ipc_close(IpcHandle h);

// No-op for TCP. Kept for API compatibility.
void ipc_set_permissions(const std::string& path, int mode);

}  // namespace quantclaw::platform
