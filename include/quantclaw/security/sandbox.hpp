// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace quantclaw {

class Sandbox {
 private:
  std::filesystem::path workspace_path_;
  std::vector<std::string> allowed_paths_;
  std::vector<std::string> denied_paths_;
  std::vector<std::string> allowed_commands_;
  std::vector<std::string> denied_commands_;
  std::vector<std::regex> denied_cmd_patterns_;

 public:
  Sandbox(const std::filesystem::path& workspace_path,
          const std::vector<std::string>& allowed_paths,
          const std::vector<std::string>& denied_paths,
          const std::vector<std::string>& allowed_commands,
          const std::vector<std::string>& denied_commands);

  bool IsPathAllowed(const std::string& path) const;
  bool IsCommandAllowed(const std::string& command) const;
  std::string SanitizePath(const std::string& path) const;

  // Static convenience methods used by ToolRegistry and MCPServer
  static bool ValidateFilePath(const std::string& path,
                               const std::string& workspace);
  static bool ValidateShellCommand(const std::string& command);
  static void ApplyResourceLimits();
};

using SecuritySandbox = Sandbox;

}  // namespace quantclaw
