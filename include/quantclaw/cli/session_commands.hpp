// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>
#include "quantclaw/constants.hpp"

namespace quantclaw::cli {

class SessionCommands {
public:
    explicit SessionCommands(std::shared_ptr<spdlog::logger> logger);

    int ListCommand(const std::vector<std::string>& args);
    int HistoryCommand(const std::vector<std::string>& args);
    int DeleteCommand(const std::vector<std::string>& args);
    int ResetCommand(const std::vector<std::string>& args);

    void SetGatewayUrl(const std::string& url) { gateway_url_ = url; }

private:
    std::shared_ptr<spdlog::logger> logger_;
    std::string gateway_url_ = kDefaultGatewayUrl;
};

} // namespace quantclaw::cli
