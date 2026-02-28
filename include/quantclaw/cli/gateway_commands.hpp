#pragma once

#include <string>
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>

namespace quantclaw::cli {

class GatewayCommands {
public:
    GatewayCommands(std::shared_ptr<spdlog::logger> logger);

    // Run gateway in foreground
    int foreground_command(const std::vector<std::string>& args);

    // Daemon management
    int install_command(const std::vector<std::string>& args);
    int uninstall_command(const std::vector<std::string>& args);
    int start_command(const std::vector<std::string>& args);
    int stop_command(const std::vector<std::string>& args);
    int restart_command(const std::vector<std::string>& args);
    int status_command(const std::vector<std::string>& args);

    // RPC utility
    int call_command(const std::vector<std::string>& args);

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::string gateway_url_ = "ws://127.0.0.1:18789";
};

} // namespace quantclaw::cli
