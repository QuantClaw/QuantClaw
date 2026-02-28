#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace quantclaw::gateway {

// Manages the QuantClaw gateway as a systemd user service.
class DaemonManager {
 public:
  explicit DaemonManager(std::shared_ptr<spdlog::logger> logger);

  int install(int port = 18789);
  int uninstall();
  int start();
  int stop();
  int restart();
  int status();

  bool is_running() const;
  int get_pid() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::filesystem::path state_dir_;
  std::filesystem::path pid_file_;
  std::filesystem::path log_file_;

  std::filesystem::path service_path() const;
  std::string executable_path() const;
  void write_pid(int pid);
  void remove_pid();
};

}  // namespace quantclaw::gateway
