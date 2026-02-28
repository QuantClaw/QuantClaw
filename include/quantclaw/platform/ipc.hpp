// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace quantclaw::platform {

// Platform-neutral IPC handle
#ifdef _WIN32
using IpcHandle = void*;  // HANDLE
constexpr IpcHandle kInvalidIpc = nullptr;
#else
using IpcHandle = int;  // file descriptor
constexpr IpcHandle kInvalidIpc = -1;
#endif

// IPC transport: Unix domain socket (Linux) or Named Pipe (Windows).
// Provides a stream-oriented, bidirectional byte channel.

// Server side: create, bind, listen, accept.
class IpcServer {
 public:
  // `path` is the socket path (Unix) or pipe name (Windows).
  explicit IpcServer(const std::string& path);
  ~IpcServer();

  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;

  // Bind and listen. Returns true on success.
  bool listen();

  // Accept one client connection. Blocks up to `timeout_ms` (-1 = forever).
  // Returns a valid handle on success.
  IpcHandle accept(int timeout_ms = -1);

  // Close the listener.
  void close();

  // Remove the socket file (Unix) or pipe name cleanup.
  static void cleanup(const std::string& path);

  const std::string& path() const { return path_; }

 private:
  std::string path_;
  IpcHandle listen_handle_ = kInvalidIpc;
};

// Client side: connect to a server.
class IpcClient {
 public:
  explicit IpcClient(const std::string& path);
  ~IpcClient();

  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  // Connect to server. Returns true on success.
  bool connect();

  // Get the connection handle (for read/write).
  IpcHandle handle() const { return handle_; }

  // Close the connection.
  void close();

 private:
  std::string path_;
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

// Set socket/pipe permissions (e.g. 0600). No-op on Windows.
void ipc_set_permissions(const std::string& path, int mode);

}  // namespace quantclaw::platform
