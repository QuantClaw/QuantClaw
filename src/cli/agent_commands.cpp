#include "quantclaw/cli/agent_commands.hpp"
#include "quantclaw/gateway/gateway_client.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include <iostream>

namespace quantclaw::cli {

AgentCommands::AgentCommands(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
}

int AgentCommands::request_command(const std::vector<std::string>& args) {
    std::string message;
    std::string session_key = "agent:main:main";
    std::string model;
    bool json_output = false;
    int timeout_ms = 120000;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size()) {
            message = args[++i];
        } else if ((args[i] == "-s" || args[i] == "--session" ||
                     args[i] == "--session-id") && i + 1 < args.size()) {
            session_key = args[++i];
        } else if (args[i] == "--model" && i + 1 < args.size()) {
            model = args[++i];
        } else if (args[i] == "--timeout" && i + 1 < args.size()) {
            timeout_ms = std::stoi(args[++i]) * 1000;
        } else if (args[i] == "--json") {
            json_output = true;
        } else if (args[i][0] != '-' && message.empty()) {
            message = args[i];
        }
    }

    if (message.empty()) {
        std::cerr << "Usage: quantclaw agent -m \"your message\" [--session-id <id>] "
                     "[--timeout <seconds>] [--json]" << std::endl;
        return 1;
    }

    try {
        auto client = std::make_shared<gateway::GatewayClient>(gateway_url_, "", logger_);
        if (!client->connect()) {
            std::cerr << "Error: Cannot connect to gateway at " << gateway_url_ << std::endl;
            std::cerr << "Is the gateway running? Start it with: quantclaw gateway" << std::endl;
            return 1;
        }

        // Subscribe to streaming events (print text deltas in real-time)
        if (!json_output) {
            client->subscribe("agent.text_delta",
                [](const std::string&, const nlohmann::json& payload) {
                    if (payload.contains("text")) {
                        std::cout << payload["text"].get<std::string>() << std::flush;
                    }
                }
            );
            client->subscribe("agent.message_end",
                [](const std::string&, const nlohmann::json&) {
                    std::cout << std::endl;
                }
            );
        }

        nlohmann::json params = {
            {"sessionKey", session_key},
            {"message", message}
        };
        if (!model.empty()) {
            params["model"] = model;
        }

        auto result = client->call("agent.request", params, timeout_ms);

        if (json_output) {
            std::cout << result.dump(2) << std::endl;
        }

        client->disconnect();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int AgentCommands::stop_command(const std::vector<std::string>& /*args*/) {
    try {
        auto client = std::make_shared<gateway::GatewayClient>(gateway_url_, "", logger_);
        if (!client->connect()) {
            std::cerr << "Error: Cannot connect to gateway" << std::endl;
            return 1;
        }

        auto result = client->call("agent.stop", {});
        std::cout << "Agent stopped" << std::endl;

        client->disconnect();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

} // namespace quantclaw::cli
