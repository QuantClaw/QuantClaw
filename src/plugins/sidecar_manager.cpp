// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/plugins/sidecar_manager.hpp"

#include <algorithm>
#include <fstream>

namespace quantclaw {

namespace {
constexpr int kMaxBackoffMs = 60000;
constexpr int kBaseBackoffMs = 1000;
}  // namespace

nlohmann::json SidecarRequest::to_json() const {
  return {
      {"jsonrpc", "2.0"},
      {"method", method},
      {"params", params},
      {"id", id},
  };
}

SidecarResponse SidecarResponse::FromJson(const nlohmann::json& j) {
  SidecarResponse r;
  r.id = j.value("id", 0);
  if (j.contains("error")) {
    r.ok = false;
    if (j["error"].is_object()) {
      r.error = j["error"].value("message", j["error"].dump());
    } else {
      r.error = j["error"].dump();
    }
  } else {
    r.result = j.value("result", nlohmann::json{});
  }
  return r;
}

SidecarManager::SidecarManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

SidecarManager::~SidecarManager() {
  Stop();
}

bool SidecarManager::Start(const Options& opts) {
  if (running_) {
    logger_->warn("Sidecar already running (pid={})", pid_.load());
    return true;
  }

  opts_ = opts;

  if (opts_.pid_file.empty()) {
    std::string home = platform::home_directory();
    opts_.pid_file = home + "/.quantclaw/sidecar.pid";
  }

  if (!spawn_sidecar()) {
    return false;
  }

  running_ = true;
  stopping_ = false;
  restart_count_ = 0;

  monitor_thread_ = std::thread([this] { monitor_loop(); });

  return true;
}

void SidecarManager::Stop() {
  if (!running_)
    return;

  stopping_ = true;
  running_ = false;

  kill_sidecar(false);

  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }

  // Cleanup IPC
  {
    std::lock_guard<std::mutex> lock(ipc_mu_);
    if (ipc_handle_ != platform::kInvalidIpc) {
      platform::ipc_close(ipc_handle_);
      ipc_handle_ = platform::kInvalidIpc;
    }
    ipc_port_ = 0;
  }
  remove_pid_file();
}

bool SidecarManager::Reload() {
  if (!IsRunning())
    return false;

  auto p = pid_.load();
  if (p != platform::kInvalidPid) {
    logger_->info("Sending reload signal to sidecar (pid={})", p);
    platform::reload_process(p);
    return true;
  }
  return false;
}

SidecarResponse SidecarManager::Call(const std::string& method,
                                     const nlohmann::json& params,
                                     int timeout_ms) {
  if (!IsRunning()) {
    SidecarResponse r;
    r.ok = false;
    r.error = "Sidecar not running";
    return r;
  }

  std::lock_guard<std::mutex> lock(ipc_mu_);

  if (ipc_handle_ == platform::kInvalidIpc && !connect_ipc()) {
    SidecarResponse r;
    r.ok = false;
    r.error = "Cannot connect to sidecar";
    return r;
  }

  SidecarRequest req;
  req.method = method;
  req.params = params;
  req.id = rpc_id_.fetch_add(1);

  std::string payload = req.to_json().dump() + "\n";
  int written = platform::ipc_write(ipc_handle_, payload.data(),
                                    static_cast<int>(payload.size()));
  if (written < 0 || static_cast<size_t>(written) != payload.size()) {
    platform::ipc_close(ipc_handle_);
    ipc_handle_ = platform::kInvalidIpc;
    SidecarResponse r;
    r.ok = false;
    r.error = "Write to sidecar failed";
    return r;
  }

  std::string response_line = platform::ipc_read_line(ipc_handle_, timeout_ms);
  if (response_line.empty()) {
    platform::ipc_close(ipc_handle_);
    ipc_handle_ = platform::kInvalidIpc;
    SidecarResponse r;
    r.ok = false;
    r.error = "Sidecar response timeout";
    return r;
  }

  try {
    auto j = nlohmann::json::parse(response_line);
    return SidecarResponse::FromJson(j);
  } catch (const std::exception& e) {
    SidecarResponse r;
    r.ok = false;
    r.error = std::string("Invalid sidecar response: ") + e.what();
    return r;
  }
}

bool SidecarManager::IsRunning() const {
  auto p = pid_.load();
  if (p == platform::kInvalidPid)
    return false;
  return platform::is_process_alive(p);
}

void SidecarManager::monitor_loop() {
  while (running_ && !stopping_) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(opts_.heartbeat_interval_ms));

    if (stopping_)
      break;

    if (!IsRunning()) {
      if (stopping_)
        break;

      logger_->warn("Sidecar process died unexpectedly");
      pid_ = platform::kInvalidPid;

      if (restart_count_ >= opts_.max_restarts) {
        logger_->error("Sidecar max restarts ({}) exceeded, giving up",
                       opts_.max_restarts);
        running_ = false;
        break;
      }

      int backoff = next_backoff_ms();
      logger_->info("Restarting sidecar in {}ms (attempt {}/{})", backoff,
                    restart_count_ + 1, opts_.max_restarts);
      std::this_thread::sleep_for(std::chrono::milliseconds(backoff));

      if (stopping_)
        break;

      {
        std::lock_guard<std::mutex> lock(ipc_mu_);
        if (ipc_handle_ != platform::kInvalidIpc) {
          platform::ipc_close(ipc_handle_);
          ipc_handle_ = platform::kInvalidIpc;
        }
        ipc_port_ = 0;
      }

      if (spawn_sidecar()) {
        restart_count_++;
        last_restart_ = std::chrono::steady_clock::now();
      } else {
        logger_->error("Failed to restart sidecar");
      }
      continue;
    }

    // Heartbeat check via RPC ping
    auto resp = Call("ping", {}, 5000);
    if (!resp.ok) {
      logger_->warn("Sidecar heartbeat failed: {}", resp.error);
    }
  }
}

bool SidecarManager::spawn_sidecar() {
  if (opts_.sidecar_script.empty()) {
    logger_->error("No sidecar script configured");
    return false;
  }

  // Create TCP IPC server — OS assigns a free loopback port.
  platform::IpcServer server;
  if (!server.listen()) {
    logger_->error("Failed to create TCP IPC server");
    return false;
  }
  int port = server.port();
  logger_->debug("IPC TCP server listening on 127.0.0.1:{}", port);

  // Build env vars — pass port instead of socket path.
  std::vector<std::string> env;
  env.push_back("QUANTCLAW_PORT=" + std::to_string(port));
  if (!opts_.plugin_config.is_null()) {
    env.push_back("QUANTCLAW_PLUGIN_CONFIG=" + opts_.plugin_config.dump());
  }

  // Spawn child process
  std::vector<std::string> args = {opts_.node_binary, opts_.sidecar_script};
  auto child = platform::spawn_process(args, env);
  if (child == platform::kInvalidPid) {
    logger_->error("Failed to spawn sidecar process");
    server.close();
    return false;
  }

  pid_ = child;
  write_pid_file();
  logger_->info("Sidecar started (pid={})", child);

  // Accept connection from sidecar with timeout
  auto connected = server.accept(10000);
  server.close();

  if (connected == platform::kInvalidIpc) {
    logger_->error("Sidecar did not connect within 10 seconds");
    platform::kill_process(child);
    platform::wait_process(child, 5000);
    pid_ = platform::kInvalidPid;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(ipc_mu_);
    ipc_handle_ = connected;
    ipc_port_ = port;
  }

  return true;
}

void SidecarManager::kill_sidecar(bool force) {
  auto p = pid_.load();
  if (p == platform::kInvalidPid)
    return;

  if (force) {
    logger_->info("Force killing sidecar (pid={})", p);
    platform::kill_process(p);
    platform::wait_process(p, 5000);
  } else {
    logger_->info("Gracefully stopping sidecar (pid={})", p);
    platform::terminate_process(p);

    int exit_code = platform::wait_process(p, opts_.graceful_stop_timeout_ms);
    if (exit_code >= 0) {
      logger_->info("Sidecar exited (status={})", exit_code);
    } else {
      logger_->warn("Sidecar did not exit within {}ms, force killing",
                    opts_.graceful_stop_timeout_ms);
      platform::kill_process(p);
      platform::wait_process(p, 5000);
    }
  }
  pid_ = platform::kInvalidPid;
}

bool SidecarManager::connect_ipc() {
  if (ipc_port_ <= 0)
    return false;
  platform::IpcClient client("127.0.0.1", ipc_port_);
  if (!client.connect())
    return false;
  ipc_handle_ = client.handle();
  return true;
}

void SidecarManager::write_pid_file() {
  if (opts_.pid_file.empty())
    return;
  auto parent = std::filesystem::path(opts_.pid_file).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream ofs(opts_.pid_file);
  if (ofs.is_open()) {
    ofs << pid_.load();
  }
}

void SidecarManager::remove_pid_file() {
  if (!opts_.pid_file.empty()) {
    std::filesystem::remove(opts_.pid_file);
  }
}

int SidecarManager::next_backoff_ms() {
  int backoff = kBaseBackoffMs * (1 << std::min(restart_count_, 6));
  return std::min(backoff, kMaxBackoffMs);
}

}  // namespace quantclaw
