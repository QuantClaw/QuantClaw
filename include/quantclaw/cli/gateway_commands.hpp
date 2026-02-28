// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

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
    int ForegroundCommand(const std::vector<std::string>& args);

    // Daemon management
    int InstallCommand(const std::vector<std::string>& args);
    int UninstallCommand(const std::vector<std::string>& args);
    int StartCommand(const std::vector<std::string>& args);
    int StopCommand(const std::vector<std::string>& args);
    int RestartCommand(const std::vector<std::string>& args);
    int StatusCommand(const std::vector<std::string>& args);

    // RPC utility
    int CallCommand(const std::vector<std::string>& args);

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::string gateway_url_ = "ws://127.0.0.1:18789";
};

} // namespace quantclaw::cli
