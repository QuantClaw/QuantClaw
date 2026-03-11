// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace quantclaw::cli {

class CLIManager {
 public:
  struct Command {
    std::string name;
    std::string description;
    std::vector<std::string> aliases;
    std::function<int(int, char**)> handler;
  };

  CLIManager();

  void AddCommand(const Command& command);
  int Run(int argc, char** argv);
  void ShowHelp() const;

 private:
  std::vector<Command> commands_;
};

}  // namespace quantclaw::cli