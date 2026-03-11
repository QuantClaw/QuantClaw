// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <spdlog/logger.h>

namespace quantclaw::cli {

class OnboardCommands {
 public:
  explicit OnboardCommands(std::shared_ptr<spdlog::logger> logger);

  int OnboardCommand(const std::vector<std::string>& args);
  int InstallDaemonCommand(const std::vector<std::string>& args);
  int QuickSetupCommand(const std::vector<std::string>& args);

 private:
  std::shared_ptr<spdlog::logger> logger_;

  // Wizard steps
  void PrintWelcome();
  void PrintStep(int current, int total, const std::string& title);
  std::string PromptString(const std::string& prompt,
                           const std::string& default_value = "");
  bool PromptYesNo(const std::string& prompt, bool default_value = true);
  std::string PromptChoice(const std::string& prompt,
                           const std::vector<std::string>& choices);

  // Setup steps
  int SetupConfig();
  int SetupWorkspace();
  int SetupDaemon();
  int SetupSkills();
  int SetupChannels();
  int VerifySetup();

  bool CreateWorkspaceDirectory();
  bool CreateConfigFile(const std::string& model, int port,
                        const std::string& bind, const std::string& token);
  bool CreateWorkspaceFile(const std::string& filename,
                           const std::string& content);
  bool CreateSOULFile();
  bool CreateMemoryFile();
  bool CreateSkillFile();
  bool CreateIdentityFile();
  bool CreateHeartbeatFile();
  bool CreateUserFile();
  bool CreateAgentsFile();
  bool CreateToolsFile();
  bool InstallDaemon(int port);
  bool TestGatewayConnection(int port);
  static std::string GenerateToken();
};

}  // namespace quantclaw::cli
