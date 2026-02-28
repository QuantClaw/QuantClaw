#include "quantclaw/plugins/sidecar_manager.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace quantclaw {

namespace {

constexpr int kMaxBackoffMs = 60000;
constexpr int kBaseBackoffMs = 1000;
constexpr size_t kMaxResponseSize = 16 * 1024 * 1024;  // 16 MB

std::string read_line(int fd, int timeout_ms) {
  std::string line;
  line.reserve(4096);
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) break;

    struct timeval tv;
    tv.tv_sec = remaining.count() / 1000;
    tv.tv_usec = (remaining.count() % 1000) * 1000;

    int ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) break;

    char c;
    ssize_t n = read(fd, &c, 1);
    if (n <= 0) break;
    if (c == '\n') return line;
    line += c;

    if (line.size() > kMaxResponseSize) break;
  }
  return line;
}

}  // namespace

nlohmann::json SidecarRequest::to_json() const {
  return {
      {"jsonrpc", "2.0"},
      {"method", method},
      {"params", params},
      {"id", id},
  };
}

SidecarResponse SidecarResponse::from_json(const nlohmann::json& j) {
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
  stop();
}

bool SidecarManager::start(const Options& opts) {
  if (running_) {
    logger_->warn("Sidecar already running (pid={})", pid_.load());
    return true;
  }

  opts_ = opts;

  if (opts_.socket_path.empty()) {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    opts_.socket_path = home_str + "/.quantclaw/sidecar.sock";
  }
  if (opts_.pid_file.empty()) {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    opts_.pid_file = home_str + "/.quantclaw/sidecar.pid";
  }

  // Remove stale socket
  unlink(opts_.socket_path.c_str());

  if (!spawn_process()) {
    return false;
  }

  running_ = true;
  stopping_ = false;
  restart_count_ = 0;

  // Start monitor thread
  monitor_thread_ = std::thread([this] { monitor_loop(); });

  return true;
}

void SidecarManager::stop() {
  if (!running_) return;

  stopping_ = true;
  running_ = false;

  kill_process(false);

  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }

  // Cleanup
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  unlink(opts_.socket_path.c_str());
  remove_pid_file();
}

bool SidecarManager::reload() {
  if (!is_running()) return false;

  pid_t p = pid_.load();
  if (p > 0) {
    logger_->info("Sending SIGHUP to sidecar (pid={})", p);
    return kill(p, SIGHUP) == 0;
  }
  return false;
}

SidecarResponse SidecarManager::call(const std::string& method,
                                     const nlohmann::json& params,
                                     int timeout_ms) {
  if (!is_running()) {
    SidecarResponse r;
    r.ok = false;
    r.error = "Sidecar not running";
    return r;
  }

  std::lock_guard<std::mutex> lock(socket_mu_);

  if (socket_fd_ < 0 && !connect_socket()) {
    SidecarResponse r;
    r.ok = false;
    r.error = "Cannot connect to sidecar socket";
    return r;
  }

  SidecarRequest req;
  req.method = method;
  req.params = params;
  req.id = rpc_id_.fetch_add(1);

  std::string payload = req.to_json().dump() + "\n";
  ssize_t written = write(socket_fd_, payload.data(), payload.size());
  if (written < 0 || static_cast<size_t>(written) != payload.size()) {
    close(socket_fd_);
    socket_fd_ = -1;
    SidecarResponse r;
    r.ok = false;
    r.error = "Write to sidecar failed: " + std::string(strerror(errno));
    return r;
  }

  std::string response_line = read_line(socket_fd_, timeout_ms);
  if (response_line.empty()) {
    close(socket_fd_);
    socket_fd_ = -1;
    SidecarResponse r;
    r.ok = false;
    r.error = "Sidecar response timeout";
    return r;
  }

  try {
    auto j = nlohmann::json::parse(response_line);
    return SidecarResponse::from_json(j);
  } catch (const std::exception& e) {
    SidecarResponse r;
    r.ok = false;
    r.error = std::string("Invalid sidecar response: ") + e.what();
    return r;
  }
}

bool SidecarManager::is_running() const {
  pid_t p = pid_.load();
  if (p <= 0) return false;
  return kill(p, 0) == 0;
}

void SidecarManager::monitor_loop() {
  while (running_ && !stopping_) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(opts_.heartbeat_interval_ms));

    if (stopping_) break;

    if (!is_running()) {
      if (stopping_) break;

      // Process died unexpectedly
      logger_->warn("Sidecar process died unexpectedly");
      pid_ = 0;

      if (restart_count_ >= opts_.max_restarts) {
        logger_->error("Sidecar max restarts ({}) exceeded, giving up",
                       opts_.max_restarts);
        running_ = false;
        break;
      }

      int backoff = next_backoff_ms();
      logger_->info("Restarting sidecar in {}ms (attempt {}/{})",
                    backoff, restart_count_ + 1, opts_.max_restarts);
      std::this_thread::sleep_for(std::chrono::milliseconds(backoff));

      if (stopping_) break;

      if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
      }
      unlink(opts_.socket_path.c_str());

      if (spawn_process()) {
        restart_count_++;
        last_restart_ = std::chrono::steady_clock::now();
      } else {
        logger_->error("Failed to restart sidecar");
      }
      continue;
    }

    // Heartbeat check via RPC ping
    auto resp = call("ping", {}, 5000);
    if (!resp.ok) {
      logger_->warn("Sidecar heartbeat failed: {}", resp.error);
    }
  }
}

bool SidecarManager::spawn_process() {
  if (opts_.sidecar_script.empty()) {
    logger_->error("No sidecar script configured");
    return false;
  }

  // Create the socket before forking so sidecar can connect
  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    logger_->error("Failed to create Unix socket: {}", strerror(errno));
    return false;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, opts_.socket_path.c_str(),
          sizeof(addr.sun_path) - 1);

  unlink(opts_.socket_path.c_str());
  if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    logger_->error("Failed to bind socket {}: {}",
                   opts_.socket_path, strerror(errno));
    close(server_fd);
    return false;
  }

  // Set socket permissions to 0600
  chmod(opts_.socket_path.c_str(), 0600);

  if (listen(server_fd, 1) < 0) {
    logger_->error("Failed to listen on socket: {}", strerror(errno));
    close(server_fd);
    return false;
  }

  pid_t child = fork();
  if (child < 0) {
    logger_->error("Fork failed: {}", strerror(errno));
    close(server_fd);
    return false;
  }

  if (child == 0) {
    // Child process: exec Node.js sidecar
    close(server_fd);

    // Set environment
    setenv("QUANTCLAW_SOCKET", opts_.socket_path.c_str(), 1);
    if (!opts_.plugin_config.is_null()) {
      setenv("QUANTCLAW_PLUGIN_CONFIG", opts_.plugin_config.dump().c_str(), 1);
    }

    execlp(opts_.node_binary.c_str(), opts_.node_binary.c_str(),
           opts_.sidecar_script.c_str(), nullptr);

    // exec failed
    _exit(127);
  }

  // Parent: wait for sidecar to connect
  pid_ = child;
  write_pid_file();
  logger_->info("Sidecar started (pid={})", child);

  // Accept connection from sidecar with timeout
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(server_fd, &fds);
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;

  int ret = select(server_fd + 1, &fds, nullptr, nullptr, &tv);
  if (ret <= 0) {
    logger_->error("Sidecar did not connect within 10 seconds");
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    pid_ = 0;
    close(server_fd);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(socket_mu_);
    socket_fd_ = accept(server_fd, nullptr, nullptr);
  }
  close(server_fd);

  if (socket_fd_ < 0) {
    logger_->error("Failed to accept sidecar connection: {}",
                   strerror(errno));
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    pid_ = 0;
    return false;
  }

  return true;
}

void SidecarManager::kill_process(bool force) {
  pid_t p = pid_.load();
  if (p <= 0) return;

  if (force) {
    logger_->info("Force killing sidecar (pid={})", p);
    kill(p, SIGKILL);
  } else {
    logger_->info("Gracefully stopping sidecar (pid={})", p);
    kill(p, SIGTERM);

    // Wait for graceful shutdown
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(opts_.graceful_stop_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      int status;
      pid_t result = waitpid(p, &status, WNOHANG);
      if (result > 0) {
        logger_->info("Sidecar exited (status={})", WEXITSTATUS(status));
        pid_ = 0;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Graceful timeout expired, force kill
    logger_->warn("Sidecar did not exit within {}ms, sending SIGKILL",
                  opts_.graceful_stop_timeout_ms);
    kill(p, SIGKILL);
    waitpid(p, nullptr, 0);
  }
  pid_ = 0;
}

bool SidecarManager::connect_socket() {
  socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd_ < 0) return false;

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, opts_.socket_path.c_str(),
          sizeof(addr.sun_path) - 1);

  if (connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr),
              sizeof(addr)) < 0) {
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }
  return true;
}

void SidecarManager::write_pid_file() {
  if (opts_.pid_file.empty()) return;
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
