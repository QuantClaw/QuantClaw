// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32

#include <filesystem>
#include <fstream>

#include "quantclaw/platform/process.hpp"
#include "quantclaw/platform/service.hpp"

namespace quantclaw::platform {

ServiceManager::ServiceManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  state_dir_ = home_directory() + "\\.quantclaw";
  pid_file_ = state_dir_ + "\\gateway.pid";
  log_file_ = state_dir_ + "\\logs\\gateway.log";
  std::filesystem::create_directories(state_dir_ + "\\logs");
}

std::string ServiceManager::service_path() const {
  // Not applicable on Windows
  return state_dir_ + "\\gateway.service.json";
}

int ServiceManager::install(int port) {
  // On Windows, write a JSON config that records the service parameters.
  // The actual "service" is started as a background process.
  auto svc = service_path();
  std::ofstream out(svc);
  if (!out) {
    logger_->error("Cannot write service config: {}", svc);
    return 1;
  }

  std::string exe = executable_path();
  out << "{\n"
      << "  \"executable\": \"" << exe << "\",\n"
      << "  \"args\": [\"gateway\", \"run\", \"--port\", \"" << port << "\"],\n"
      << "  \"logFile\": \"" << log_file_ << "\"\n"
      << "}\n";
  out.close();

  logger_->info("Service config written to {}", svc);
  return 0;
}

int ServiceManager::uninstall() {
  stop();
  auto svc = service_path();
  if (std::filesystem::exists(svc)) {
    std::filesystem::remove(svc);
  }
  logger_->info("Service config removed");
  return 0;
}

int ServiceManager::start() {
  std::string exe = executable_path();
  std::vector<std::string> args = {exe, "gateway", "run"};
  ProcessId pid = spawn_process(args);
  if (pid == kInvalidPid) {
    logger_->error("Failed to start gateway");
    return 1;
  }
  write_pid(static_cast<int>(pid));
  logger_->info("Gateway started (PID: {})", pid);
  return 0;
}

int ServiceManager::stop() {
  int pid = get_pid();
  if (pid <= 0) {
    logger_->info("Gateway not running");
    return 0;
  }

  terminate_process(static_cast<ProcessId>(pid));
  int exit_code = wait_process(static_cast<ProcessId>(pid), 10000);
  if (exit_code < 0) {
    kill_process(static_cast<ProcessId>(pid));
  }
  remove_pid();
  logger_->info("Gateway stopped");
  return 0;
}

int ServiceManager::restart() {
  stop();
  return start();
}

int ServiceManager::status() {
  int pid = get_pid();
  if (pid <= 0 || !is_process_alive(static_cast<ProcessId>(pid))) {
    logger_->info("Gateway is not running");
    return 1;
  }
  logger_->info("Gateway is running (PID: {})", pid);
  return 0;
}

bool ServiceManager::is_running() const {
  int pid = get_pid();
  if (pid <= 0)
    return false;
  return is_process_alive(static_cast<ProcessId>(pid));
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

#endif  // _WIN32
