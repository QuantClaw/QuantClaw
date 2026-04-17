// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.plugins.sidecar_manager;

import std;
import nlohmann.json;
import quantclaw.platform.ipc;
import quantclaw.platform.process;

export namespace quantclaw {

struct SidecarRequest {
  std::string method;
  nlohmann::json params;
  int id = 0;

  nlohmann::json to_json() const;
};

struct SidecarResponse {
  int id = 0;
  nlohmann::json result;
  std::string error;
  bool ok = true;

  static SidecarResponse FromJson(const nlohmann::json& j);
};

class SidecarManager {
 public:
  explicit SidecarManager(std::shared_ptr<spdlog::logger> logger);
  ~SidecarManager();

  SidecarManager(const SidecarManager&) = delete;
  SidecarManager& operator=(const SidecarManager&) = delete;

  struct Options {
    std::string node_binary = "node";
    std::string sidecar_script;
    std::string pid_file;
    int heartbeat_interval_ms = 5000;
    int heartbeat_timeout_count = 3;
    std::string heartbeat_method = "ping";
    int graceful_stop_timeout_ms = 10000;
    int max_restarts = 10;
    std::vector<std::string> env_whitelist;
    nlohmann::json plugin_config;
    std::string socket_path;
  };

  bool Start(const Options& opts);
  void Stop();
  bool Reload();
  SidecarResponse Call(const std::string& method, const nlohmann::json& params,
                       int timeout_ms = 30000);
  bool IsRunning() const;

  platform::ProcessId pid() const { return pid_; }

 private:
  void monitor_loop();
  bool spawn_sidecar();
  void kill_sidecar(bool force = false);
  bool connect_ipc();
  void write_pid_file();
  void remove_pid_file();
  int next_backoff_ms();

  std::shared_ptr<spdlog::logger> logger_;
  Options opts_;
  std::atomic<platform::ProcessId> pid_{platform::kInvalidPid};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  platform::IpcHandle ipc_handle_ = platform::kInvalidIpc;
  int ipc_port_ = 0;
  std::mutex ipc_mu_;
  std::thread monitor_thread_;
  int restart_count_ = 0;
  std::chrono::steady_clock::time_point last_restart_;
  std::atomic<int> rpc_id_{1};
};

}  // namespace quantclaw