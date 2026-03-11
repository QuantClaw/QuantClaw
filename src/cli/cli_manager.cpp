// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/cli/cli_manager.hpp"

#include <algorithm>
#include <iostream>

#include <spdlog/spdlog.h>

#include "quantclaw/constants.hpp"

namespace quantclaw::cli {

CLIManager::CLIManager() = default;

void CLIManager::AddCommand(const Command& command) {
  commands_.push_back(command);
}

int CLIManager::Run(int argc, char** argv) {
  if (argc < 2) {
    ShowHelp();
    return 1;
  }

  std::string command_name = argv[1];

  // Handle global flags
  if (command_name == "--version" || command_name == "-v") {
    std::cout << "quantclaw " << kVersion << " (build " << kGitCommit << " "
              << kBuildDate << ")" << std::endl;
    return 0;
  }
  if (command_name == "--help" || command_name == "-h") {
    ShowHelp();
    return 0;
  }

  // Find and run the command
  for (const auto& cmd : commands_) {
    if (cmd.name == command_name ||
        std::find(cmd.aliases.begin(), cmd.aliases.end(), command_name) !=
            cmd.aliases.end()) {
      char** cmd_argv = &argv[1];
      int cmd_argc = argc - 1;
      return cmd.handler(cmd_argc, cmd_argv);
    }
  }

  std::cerr << "Unknown command: " << command_name << std::endl;
  ShowHelp();
  return 1;
}

void CLIManager::ShowHelp() const {
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
        if (i > 0)
          std::cout << ", ";
        std::cout << cmd.aliases[i];
      }
      std::cout << ")";
    }
    std::cout << "\t" << cmd.description << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Global flags:" << std::endl;
  std::cout << "  --version, -v\tPrint version" << std::endl;
  std::cout << "  --help, -h\tShow help" << std::endl;
  std::cout << "  --json\tJSON output mode" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  quantclaw gateway              Start gateway (foreground)"
            << std::endl;
  std::cout << "  quantclaw gateway install       Install as system service"
            << std::endl;
  std::cout << "  quantclaw gateway status         Show gateway status"
            << std::endl;
  std::cout << "  quantclaw agent -m \"Hello\"       Send message to agent"
            << std::endl;
  std::cout << "  quantclaw sessions list          List sessions" << std::endl;
  std::cout << "  quantclaw health                 Health check" << std::endl;
  std::cout << "  quantclaw config get gateway.port Get config value"
            << std::endl;
}

}  // namespace quantclaw::cli
