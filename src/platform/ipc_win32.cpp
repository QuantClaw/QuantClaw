// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32

#include "quantclaw/platform/ipc.hpp"

#include <chrono>
#include <thread>
#include <windows.h>

namespace quantclaw::platform {

// On Windows, IpcHandle is HANDLE (void*).
// We use Named Pipes for IPC.

static std::string to_pipe_name(const std::string& path) {
  // Convert a Unix-style socket path to a Windows Named Pipe name.
  // e.g. "/home/user/.quantclaw/sidecar.sock" -> "\\\\.\\pipe\\quantclaw_sidecar"
  // If already a pipe name, use as-is.
  if (path.find("\\\\.\\pipe\\") == 0) return path;

  // Extract filename without extension as pipe name
  std::string name = path;
  auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  auto dot = name.rfind('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  return "\\\\.\\pipe\\quantclaw_" + name;
}

// --- IpcServer ---

IpcServer::IpcServer(const std::string& path) : path_(path) {}

IpcServer::~IpcServer() { close(); }

bool IpcServer::listen() {
  std::string pipe_name = to_pipe_name(path_);
  listen_handle_ = CreateNamedPipeA(
      pipe_name.c_str(),
      PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,      // max instances
      65536,  // out buffer
      65536,  // in buffer
      0,      // default timeout
      nullptr);

  return listen_handle_ != INVALID_HANDLE_VALUE;
}

IpcHandle IpcServer::accept(int timeout_ms) {
  if (!listen_handle_ || listen_handle_ == INVALID_HANDLE_VALUE)
    return kInvalidIpc;

  // ConnectNamedPipe blocks until a client connects.
  // For timeout support, use overlapped I/O.
  if (timeout_ms >= 0) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return kInvalidIpc;

    BOOL connected = ConnectNamedPipe(listen_handle_, &ov);
    if (!connected) {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(
            ov.hEvent, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        CloseHandle(ov.hEvent);
        if (wait != WAIT_OBJECT_0) {
          CancelIo(listen_handle_);
          return kInvalidIpc;
        }
      } else if (err != ERROR_PIPE_CONNECTED) {
        CloseHandle(ov.hEvent);
        return kInvalidIpc;
      } else {
        CloseHandle(ov.hEvent);
      }
    } else {
      CloseHandle(ov.hEvent);
    }
  } else {
    BOOL connected = ConnectNamedPipe(listen_handle_, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
      return kInvalidIpc;
    }
  }

  // The listen handle IS the connection on Windows Named Pipes
  IpcHandle result = listen_handle_;
  // Create a new pipe for the next accept
  listen_handle_ = kInvalidIpc;
  return result;
}

void IpcServer::close() {
  if (listen_handle_ && listen_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(listen_handle_);
    listen_handle_ = kInvalidIpc;
  }
}

void IpcServer::cleanup(const std::string& /*path*/) {
  // Named Pipes are automatically cleaned up on Windows
}

// --- IpcClient ---

IpcClient::IpcClient(const std::string& path) : path_(path) {}

IpcClient::~IpcClient() { close(); }

bool IpcClient::connect() {
  std::string pipe_name = to_pipe_name(path_);

  handle_ = CreateFileA(
      pipe_name.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0, nullptr, OPEN_EXISTING, 0, nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    handle_ = kInvalidIpc;
    return false;
  }

  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr);
  return true;
}

void IpcClient::close() {
  if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = kInvalidIpc;
  }
}

// --- Free functions ---

int ipc_write(IpcHandle h, const void* data, int len) {
  DWORD written;
  BOOL ok = WriteFile(h, data, (DWORD)len, &written, nullptr);
  return ok ? (int)written : -1;
}

int ipc_read(IpcHandle h, void* buf, int len) {
  DWORD read_bytes;
  BOOL ok = ReadFile(h, buf, (DWORD)len, &read_bytes, nullptr);
  return ok ? (int)read_bytes : -1;
}

std::string ipc_read_line(IpcHandle h, int timeout_ms) {
  std::string line;
  line.reserve(4096);
  constexpr size_t kMaxSize = 16 * 1024 * 1024;

  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    DWORD available = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr)) break;

    if (available == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    char c;
    DWORD read_bytes;
    if (!ReadFile(h, &c, 1, &read_bytes, nullptr) || read_bytes == 0) break;
    if (c == '\n') return line;
    line += c;
    if (line.size() > kMaxSize) break;
  }
  return line;
}

void ipc_close(IpcHandle h) {
  if (h && h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
}

void ipc_set_permissions(const std::string& /*path*/, int /*mode*/) {
  // No-op on Windows — Named Pipes use ACLs, not Unix permissions
}

}  // namespace quantclaw::platform

#endif  // _WIN32
