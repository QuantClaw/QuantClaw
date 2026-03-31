// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

module;

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

module quantclaw.platform.service;

import std;
import quantclaw.platform.process;

namespace quantclaw::platform {

static const char* kServiceName = "quantclaw-gateway";

ServiceManager::ServiceManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  state_dir_ = home_directory() + "/.quantclaw";
  pid_file_ = state_dir_ + "/gateway.pid";
  log_file_ = state_dir_ + "/logs/gateway.log";
  std::filesystem::create_directories(state_dir_ + "/logs");
}

std::string ServiceManager::service_path() const {
  return home_directory() + "/.config/systemd/user/quantclaw-gateway.service";
}

int ServiceManager::install(int port) {
  auto svc = service_path();
  std::filesystem::create_directories(std::filesystem::path(svc).parent_path());

  std::string exe = executable_path();
  std::ofstream out(svc);
  if (!out) {
    logger_->error("Cannot write service file: {}", svc);
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
      << "StandardOutput=append:" << log_file_ << "\n"
      << "StandardError=append:" << log_file_ << "\n"
      << "Environment=QUANTCLAW_LOG_LEVEL=info\n\n"
      << "[Install]\n"
      << "WantedBy=default.target\n";
  out.close();

  int r = std::system("systemctl --user daemon-reload");
  if (r != 0)
    logger_->warn("systemctl daemon-reload returned {}", r);

  (void)std::system(
      ("systemctl --user enable " + std::string(kServiceName) + " 2>/dev/null")
          .c_str());
  logger_->info("Service installed at {}", svc);
  return 0;
}

int ServiceManager::uninstall() {
  stop();
  auto svc = service_path();
  if (!std::filesystem::exists(svc)) {
    logger_->info("Service not installed");
    return 0;
  }
  (void)std::system(
      ("systemctl --user disable " + std::string(kServiceName) + " 2>/dev/null")
          .c_str());
  std::filesystem::remove(svc);
  (void)std::system("systemctl --user daemon-reload 2>/dev/null");
  logger_->info("Service uninstalled");
  return 0;
}

int ServiceManager::start() {
  std::string cmd = "systemctl --user start " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway started");
  } else {
    logger_->error("Failed to start gateway (exit {})", ret);
  }
  return ret;
}

int ServiceManager::stop() {
  std::string cmd = "systemctl --user stop " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway stopped");
    remove_pid();
  }
  return ret;
}

int ServiceManager::restart() {
  std::string cmd = "systemctl --user restart " + std::string(kServiceName);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway restarted");
  } else {
    logger_->error("Failed to restart gateway (exit {})", ret);
  }
  return ret;
}

int ServiceManager::status() {
  return std::system(("systemctl --user status " + std::string(kServiceName) +
                      " --no-pager 2>/dev/null")
                         .c_str());
}

bool ServiceManager::is_running() const {
  int pid = get_pid();
  if (pid <= 0)
    return false;
  return kill(pid, 0) == 0;
}

int ServiceManager::get_pid() const {
  if (!std::filesystem::exists(pid_file_))
    return -1;
  std::ifstream f(pid_file_);
  int pid = -1;
  f >> pid;
  return pid;
}

void ServiceManager::write_pid(int pid) {
  std::ofstream f(pid_file_);
  f << pid;
}

void ServiceManager::remove_pid() {
  std::filesystem::remove(pid_file_);
}

}  // namespace quantclaw::platform

#endif  // !_WIN32
