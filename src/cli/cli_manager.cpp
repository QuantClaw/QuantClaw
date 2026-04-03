// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module quantclaw.cli.cli_manager;

import std;

import quantclaw.constants;

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
              << kBuildDate << ")" << '\n';
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

  std::cerr << "Unknown command: " << command_name << '\n';
  ShowHelp();
  return 1;
}

void CLIManager::ShowHelp() const {
  std::cout << "QuantClaw - High-performance C++ AI assistant" << '\n';
  std::cout << '\n';
  std::cout << "Usage: quantclaw <command> [options]" << '\n';
  std::cout << '\n';
  std::cout << "Commands:" << '\n';

  for (const auto& cmd : commands_) {
    std::cout << "  " << cmd.name;
    if (!cmd.aliases.empty()) {
      std::cout << " (";
      for (std::size_t i = 0; i < cmd.aliases.size(); ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << cmd.aliases[i];
      }
      std::cout << ")";
    }
    std::cout << "\t" << cmd.description << '\n';
  }

  std::cout << '\n';
  std::cout << "Global flags:" << '\n';
  std::cout << "  --version, -v\tPrint version" << '\n';
  std::cout << "  --help, -h\tShow help" << '\n';
  std::cout << "  --json\tJSON output mode" << '\n';
  std::cout << '\n';
  std::cout << "Examples:" << '\n';
  std::cout << "  quantclaw gateway              Start gateway (foreground)"
            << '\n';
  std::cout << "  quantclaw gateway install       Install as system service"
            << '\n';
  std::cout << "  quantclaw gateway status         Show gateway status"
            << '\n';
  std::cout << "  quantclaw agent -m \"Hello\"       Send message to agent"
            << '\n';
  std::cout << "  quantclaw sessions list          List sessions" << '\n';
  std::cout << "  quantclaw health                 Health check" << '\n';
  std::cout << "  quantclaw config get gateway.port Get config value"
            << '\n';
}

}  // namespace quantclaw::cli
