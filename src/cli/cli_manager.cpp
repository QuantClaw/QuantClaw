#include "quantclaw/cli/cli_manager.hpp"
#include <iostream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace quantclaw::cli {

CLIManager::CLIManager() {
    // Commands are registered by main() after construction
}

void CLIManager::add_command(const Command& command) {
    commands_.push_back(command);
}

int CLIManager::run(int argc, char** argv) {
    if (argc < 2) {
        show_help();
        return 1;
    }

    std::string command_name = argv[1];

    // Handle global flags
    if (command_name == "--version" || command_name == "-v") {
        std::cout << "quantclaw 0.2.0" << std::endl;
        return 0;
    }
    if (command_name == "--help" || command_name == "-h") {
        show_help();
        return 0;
    }

    // Find and run the command
    for (const auto& cmd : commands_) {
        if (cmd.name == command_name ||
            std::find(cmd.aliases.begin(), cmd.aliases.end(), command_name) != cmd.aliases.end()) {
            char** cmd_argv = &argv[1];
            int cmd_argc = argc - 1;
            return cmd.handler(cmd_argc, cmd_argv);
        }
    }

    std::cerr << "Unknown command: " << command_name << std::endl;
    show_help();
    return 1;
}

void CLIManager::show_help() const {
    std::cout << "QuantClaw - High-performance C++ AI assistant" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: quantclaw <command> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;

    for (const auto& cmd : commands_) {
        std::cout << "  " << cmd.name;
        if (!cmd.aliases.empty()) {
            std::cout << " (";
            for (size_t i = 0; i < cmd.aliases.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << cmd.aliases[i];
            }
            std::cout << ")";
        }
        std::cout << "\t" << cmd.description << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Global flags:" << std::endl;
    std::cout << "  --config, -c <path>\tUse specified config file" << std::endl;
    std::cout << "  --version, -v\t\tPrint version" << std::endl;
    std::cout << "  --help, -h\t\tShow help" << std::endl;
    std::cout << "  --json\t\tJSON output mode" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  quantclaw gateway                        Start gateway (foreground)" << std::endl;
    std::cout << "  quantclaw -c /path/to/config.json gateway  Use custom config" << std::endl;
    std::cout << "  quantclaw gateway install                Install as system service" << std::endl;
    std::cout << "  quantclaw agent \"Hello\"                  Send message to agent" << std::endl;
    std::cout << "  quantclaw sessions list                  List sessions" << std::endl;
    std::cout << "  quantclaw health                         Health check" << std::endl;
    std::cout << "  quantclaw config get gateway.port        Get config value" << std::endl;
}

} // namespace quantclaw::cli
