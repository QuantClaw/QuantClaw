#include "quantclaw/gateway/daemon_manager.hpp"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace quantclaw::gateway {

static const char* kServiceName = "quantclaw-gateway";

DaemonManager::DaemonManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  const char* home = std::getenv("HOME");
  state_dir_ = std::filesystem::path(home ? home : "/tmp") / ".quantclaw";
  pid_file_ = state_dir_ / "gateway.pid";
  log_file_ = state_dir_ / "logs" / "gateway.log";
  std::filesystem::create_directories(state_dir_ / "logs");
}

std::filesystem::path DaemonManager::service_path() const {
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home) /
         ".config/systemd/user/quantclaw-gateway.service";
}

std::string DaemonManager::executable_path() const {
  char buf[4096];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    return std::string(buf);
  }
  return "quantclaw";
}

int DaemonManager::install(int port) {
  auto svc_dir = service_path().parent_path();
  std::filesystem::create_directories(svc_dir);

  std::string exe = executable_path();
  std::ofstream out(service_path());
  if (!out) {
    logger_->error("Cannot write service file: {}", service_path().string());
    return 1;
  }

  out << "[Unit]\n"
      << "Description=QuantClaw Gateway\n"
      << "After=network.target\n\n"
      << "[Service]\n"
      << "Type=simple\n"
      << "ExecStart=" << exe << " gateway run --port " << port << "\n"
      << "ExecReload=/bin/kill -HUP $MAINPID\n"
      << "Restart=on-failure\n"
      << "RestartSec=5\n"
      << "StandardOutput=append:" << log_file_.string() << "\n"
      << "StandardError=append:" << log_file_.string() << "\n"
      << "Environment=QUANTCLAW_LOG_LEVEL=info\n\n"
      << "[Install]\n"
      << "WantedBy=default.target\n";
  out.close();

  int r = std::system("systemctl --user daemon-reload");
  if (r != 0) {
    logger_->warn("systemctl daemon-reload returned {}", r);
  }
  [[maybe_unused]] int enable_ret =
      std::system(("systemctl --user enable " +
                   std::string(kServiceName) + " 2>/dev/null").c_str());
  logger_->info("Service installed at {}", service_path().string());
  return 0;
}

int DaemonManager::uninstall() {
  stop();

  auto path = service_path();
  if (!std::filesystem::exists(path)) {
    logger_->info("Service not installed");
    return 0;
  }

  [[maybe_unused]] int disable_ret =
      std::system(("systemctl --user disable " +
                   std::string(kServiceName) + " 2>/dev/null").c_str());
  std::filesystem::remove(path);
  [[maybe_unused]] int reload_ret =
      std::system("systemctl --user daemon-reload 2>/dev/null");
  logger_->info("Service uninstalled");
  return 0;
}

int DaemonManager::start() {
  std::string cmd = "systemctl --user start " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway started");
  } else {
    logger_->error("Failed to start gateway (exit {})", ret);
  }
  return ret;
}

int DaemonManager::stop() {
  std::string cmd = "systemctl --user stop " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway stopped");
    remove_pid();
  }
  return ret;
}

int DaemonManager::restart() {
  std::string cmd = "systemctl --user restart " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway restarted");
  } else {
    logger_->error("Failed to restart gateway (exit {})", ret);
  }
  return ret;
}

int DaemonManager::status() {
  return std::system(
      ("systemctl --user status " +
       std::string(kServiceName) + " --no-pager 2>/dev/null").c_str());
}

bool DaemonManager::is_running() const {
  int pid = get_pid();
  if (pid <= 0) return false;
  return kill(pid, 0) == 0;
}

int DaemonManager::get_pid() const {
  if (!std::filesystem::exists(pid_file_)) return -1;
  std::ifstream f(pid_file_);
  int pid = -1;
  f >> pid;
  return pid;
}

void DaemonManager::write_pid(int pid) {
  std::ofstream f(pid_file_);
  f << pid;
}

void DaemonManager::remove_pid() {
  std::filesystem::remove(pid_file_);
}

}  // namespace quantclaw::gateway
