// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.platform.ipc;

import std;

export namespace quantclaw::platform {

using IpcHandle = std::intptr_t;
constexpr IpcHandle kInvalidIpc = -1;

class IpcServer {
 public:
  explicit IpcServer(const std::string& path = "");
  ~IpcServer();

  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;

  bool listen();
  IpcHandle accept(int timeout_ms = -1);
  void close();
  static void cleanup(const std::string& path);

  int port() const { return port_; }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  int port_ = 0;
  IpcHandle listen_handle_ = kInvalidIpc;
};

class IpcClient {
 public:
  IpcClient(const std::string& host, int port);
  explicit IpcClient(const std::string& path);
  ~IpcClient();

  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  bool connect();
  IpcHandle handle() const { return handle_; }
  void close();

 private:
  std::string host_;
  int port_ = 0;
  IpcHandle handle_ = kInvalidIpc;
};

int ipc_write(IpcHandle h, const void* data, int len);
int ipc_read(IpcHandle h, void* buf, int len);
std::string ipc_read_line(IpcHandle h, int timeout_ms);
void ipc_close(IpcHandle h);
void ipc_set_permissions(const std::string& path, int mode);

}  // namespace quantclaw::platform