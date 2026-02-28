#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// JSON-RPC 2.0 request/response for IPC with Node.js sidecar
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

  static SidecarResponse from_json(const nlohmann::json& j);
};

// Manages the Node.js sidecar subprocess lifecycle.
// nginx-style: fork/exec, heartbeat, graceful reload/stop, crash restart.
class SidecarManager {
 public:
  explicit SidecarManager(std::shared_ptr<spdlog::logger> logger);
  ~SidecarManager();

  SidecarManager(const SidecarManager&) = delete;
  SidecarManager& operator=(const SidecarManager&) = delete;

  struct Options {
    std::string node_binary = "node";
    std::string sidecar_script;  // path to sidecar entry point
    std::string socket_path;     // Unix domain socket for IPC
    std::string pid_file;
    int heartbeat_interval_ms = 5000;
    int heartbeat_timeout_count = 3;  // miss count before declaring dead
    int graceful_stop_timeout_ms = 10000;
    int max_restarts = 10;
    std::vector<std::string> env_whitelist;
    nlohmann::json plugin_config;  // passed to sidecar as startup config
  };

  // Start the sidecar process
  bool start(const Options& opts);

  // Stop the sidecar gracefully (SIGTERM → wait → SIGKILL)
  void stop();

  // Reload plugins (SIGHUP to sidecar)
  bool reload();

  // Send a JSON-RPC request and wait for response
  SidecarResponse call(const std::string& method,
                       const nlohmann::json& params,
                       int timeout_ms = 30000);

  // Check if sidecar is alive
  bool is_running() const;

  // Get sidecar PID
  pid_t pid() const { return pid_; }

 private:
  void monitor_loop();
  bool spawn_process();
  void kill_process(bool force = false);
  bool connect_socket();
  void write_pid_file();
  void remove_pid_file();
  int next_backoff_ms();

  std::shared_ptr<spdlog::logger> logger_;
  Options opts_;

  std::atomic<pid_t> pid_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};

  int socket_fd_ = -1;
  std::mutex socket_mu_;

  std::thread monitor_thread_;
  int restart_count_ = 0;
  std::chrono::steady_clock::time_point last_restart_;

  std::atomic<int> rpc_id_{1};
};

}  // namespace quantclaw
