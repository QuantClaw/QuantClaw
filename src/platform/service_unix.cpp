// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unistd.h>

#include "quantclaw/platform/process.hpp"
#include "quantclaw/platform/service.hpp"

namespace quantclaw::platform {

namespace {

#ifdef __APPLE__
constexpr const char* kServiceLabel = "com.quantclaw.gateway";
#else
constexpr const char* kServiceLabel = "quantclaw-gateway";
#endif

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

std::string xml_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

int parse_pid_from_string(std::string_view value) {
  std::string digits;
  bool started = false;
  for (char ch : value) {
    if (ch >= '0' && ch <= '9') {
      digits += ch;
      started = true;
    } else if (started) {
      break;
    }
  }
  if (digits.empty()) {
    return -1;
  }
  try {
    return std::stoi(digits);
  } catch (...) {
    return -1;
  }
}

#ifdef __APPLE__
std::string launchd_target() {
  return "gui/" + std::to_string(static_cast<int>(geteuid())) + "/" +
         std::string(kServiceLabel);
}
#endif

}  // namespace

ServiceManager::ServiceManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  state_dir_ = home_directory() + "/.quantclaw";
  pid_file_ = state_dir_ + "/gateway.pid";
  log_file_ = state_dir_ + "/logs/gateway.log";
  std::filesystem::create_directories(state_dir_ + "/logs");
}

std::string ServiceManager::service_path() const {
#ifdef __APPLE__
  return home_directory() +
         "/Library/LaunchAgents/com.quantclaw.gateway.plist";
#else
  return home_directory() + "/.config/systemd/user/quantclaw-gateway.service";
#endif
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

#ifdef __APPLE__
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      << "<plist version=\"1.0\">\n"
      << "<dict>\n"
      << "  <key>Label</key>\n"
      << "  <string>" << xml_escape(kServiceLabel) << "</string>\n"
      << "  <key>ProgramArguments</key>\n"
      << "  <array>\n"
      << "    <string>" << xml_escape(exe) << "</string>\n"
      << "    <string>gateway</string>\n"
      << "    <string>run</string>\n"
      << "    <string>--port</string>\n"
      << "    <string>" << port << "</string>\n"
      << "  </array>\n"
      << "  <key>WorkingDirectory</key>\n"
      << "  <string>" << xml_escape(state_dir_) << "</string>\n"
      << "  <key>RunAtLoad</key>\n"
      << "  <true/>\n"
      << "  <key>KeepAlive</key>\n"
      << "  <true/>\n"
      << "  <key>StandardOutPath</key>\n"
      << "  <string>" << xml_escape(log_file_) << "</string>\n"
      << "  <key>StandardErrorPath</key>\n"
      << "  <string>" << xml_escape(log_file_) << "</string>\n"
      << "  <key>EnvironmentVariables</key>\n"
      << "  <dict>\n"
      << "    <key>HOME</key>\n"
      << "    <string>" << xml_escape(home_directory()) << "</string>\n"
      << "    <key>QUANTCLAW_LOG_LEVEL</key>\n"
      << "    <string>info</string>\n"
      << "  </dict>\n"
      << "</dict>\n"
      << "</plist>\n";
  logger_->info("launchd service installed at {}", svc);
  return 0;
#else
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
  if (r != 0) {
    logger_->warn("systemctl daemon-reload returned {}", r);
  }

  (void)std::system(
      ("systemctl --user enable " + std::string(kServiceLabel) + " 2>/dev/null")
          .c_str());
  logger_->info("Service installed at {}", svc);
  return 0;
#endif
}

int ServiceManager::uninstall() {
  stop();
  auto svc = service_path();
  if (!std::filesystem::exists(svc)) {
    logger_->info("Service not installed");
    return 0;
  }

#ifdef __APPLE__
  std::filesystem::remove(svc);
  logger_->info("launchd service uninstalled");
  return 0;
#else
  (void)std::system(
      ("systemctl --user disable " + std::string(kServiceLabel) + " 2>/dev/null")
          .c_str());
  std::filesystem::remove(svc);
  (void)std::system("systemctl --user daemon-reload 2>/dev/null");
  logger_->info("Service uninstalled");
  return 0;
#endif
}

int ServiceManager::start() {
#ifdef __APPLE__
  auto svc = service_path();
  if (!std::filesystem::exists(svc)) {
    logger_->error("launchd plist not installed: {}", svc);
    return 1;
  }

  auto target = launchd_target();
  (void)std::system(
      ("launchctl bootout " + shell_quote(target) + " >/dev/null 2>&1")
          .c_str());

  int ret = std::system(
      ("launchctl bootstrap " + shell_quote("gui/" +
                                            std::to_string(static_cast<int>(
                                                geteuid()))) +
       " " + shell_quote(svc))
          .c_str());
  if (ret != 0) {
    logger_->error("Failed to bootstrap launchd service (exit {})", ret);
    return ret;
  }

  ret = std::system(("launchctl kickstart -k " + shell_quote(target)).c_str());
  if (ret == 0) {
    logger_->info("Gateway started via launchd");
  } else {
    logger_->error("Failed to start gateway via launchd (exit {})", ret);
  }
  return ret;
#else
  std::string cmd = "systemctl --user start " + std::string(kServiceLabel);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway started");
  } else {
    logger_->error("Failed to start gateway (exit {})", ret);
  }
  return ret;
#endif
}

int ServiceManager::stop() {
#ifdef __APPLE__
  auto target = launchd_target();
  int ret = std::system(("launchctl bootout " + shell_quote(target)).c_str());
  if (ret == 0) {
    logger_->info("Gateway stopped");
    remove_pid();
  } else {
    logger_->warn("launchctl bootout returned {}", ret);
  }
  return ret;
#else
  std::string cmd = "systemctl --user stop " + std::string(kServiceLabel);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway stopped");
    remove_pid();
  }
  return ret;
#endif
}

int ServiceManager::restart() {
#ifdef __APPLE__
  auto stop_rc = stop();
  if (stop_rc != 0) {
    logger_->warn("Proceeding with launchd restart after stop returned {}",
                  stop_rc);
  }
  return start();
#else
  std::string cmd = "systemctl --user restart " + std::string(kServiceLabel);
  int ret = std::system(cmd.c_str());
  if (ret == 0) {
    logger_->info("Gateway restarted");
  } else {
    logger_->error("Failed to restart gateway (exit {})", ret);
  }
  return ret;
#endif
}

int ServiceManager::status() {
#ifdef __APPLE__
  auto result = exec_capture("launchctl print " + shell_quote(launchd_target()) +
                                 " 2>/dev/null",
                             5);
  if (!result.output.empty()) {
    std::cout << result.output;
  }
  return result.exit_code;
#else
  return std::system(("systemctl --user status " + std::string(kServiceLabel) +
                      " --no-pager 2>/dev/null")
                         .c_str());
#endif
}

bool ServiceManager::is_running() const {
  if (std::filesystem::exists(pid_file_)) {
    std::ifstream f(pid_file_);
    int pid = -1;
    f >> pid;
    if (pid > 0) {
      return kill(pid, 0) == 0;
    }
  }

#ifdef __APPLE__
  auto result = exec_capture("launchctl print " + shell_quote(launchd_target()) +
                                 " 2>/dev/null",
                             5);
  return result.exit_code == 0 &&
         result.output.find("pid = ") != std::string::npos;
#else
  auto result = exec_capture(
      "systemctl --user is-active --quiet " + std::string(kServiceLabel),
      5);
  return result.exit_code == 0;
#endif
}

int ServiceManager::get_pid() const {
  if (std::filesystem::exists(pid_file_)) {
    std::ifstream f(pid_file_);
    int pid = -1;
    f >> pid;
    if (pid > 0) {
      return pid;
    }
  }

#ifdef __APPLE__
  auto result = exec_capture("launchctl print " + shell_quote(launchd_target()) +
                                 " 2>/dev/null",
                             5);
  if (result.exit_code != 0) {
    return -1;
  }
  auto pos = result.output.find("pid = ");
  if (pos == std::string::npos) {
    return -1;
  }
  return parse_pid_from_string(
      std::string_view(result.output).substr(pos + std::string("pid = ").size()));
#else
  auto result = exec_capture(
      "systemctl --user show " + std::string(kServiceLabel) +
          " --property MainPID --value 2>/dev/null",
      5);
  if (result.exit_code != 0) {
    return -1;
  }
  int pid = parse_pid_from_string(result.output);
  return pid > 0 ? pid : -1;
#endif
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
