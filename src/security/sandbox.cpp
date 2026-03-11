// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/sandbox.hpp"

#include <filesystem>
#include <regex>

#include <spdlog/spdlog.h>
#ifdef __linux__
#include <sys/resource.h>
#endif

namespace quantclaw {

Sandbox::Sandbox(const std::filesystem::path& workspace_path,
                 const std::vector<std::string>& allowed_paths,
                 const std::vector<std::string>& denied_paths,
                 const std::vector<std::string>& allowed_commands,
                 const std::vector<std::string>& denied_commands)
    : workspace_path_(workspace_path),
      allowed_paths_(allowed_paths),
      denied_paths_(denied_paths),
      allowed_commands_(allowed_commands),
      denied_commands_(denied_commands) {
  for (const auto& cmd : denied_commands_) {
    denied_cmd_patterns_.push_back(
        std::regex(cmd, std::regex_constants::icase));
  }
}

bool Sandbox::IsPathAllowed(const std::string& path) const {
  std::filesystem::path resolved_path = std::filesystem::absolute(path);

  // Check against denied paths first
  for (const auto& denied_path : denied_paths_) {
    std::filesystem::path denied_resolved =
        std::filesystem::absolute(denied_path);
    if (resolved_path.string().find(denied_resolved.string()) == 0) {
      return false;
    }
  }

  // If allowed paths are specified, check against them
  if (!allowed_paths_.empty()) {
    for (const auto& allowed_path : allowed_paths_) {
      std::filesystem::path allowed_resolved =
          std::filesystem::absolute(allowed_path);
      if (resolved_path.string().find(allowed_resolved.string()) == 0) {
        return true;
      }
    }
    return false;
  }

  return true;
}

bool Sandbox::IsCommandAllowed(const std::string& command) const {
  for (const auto& pattern : denied_cmd_patterns_) {
    if (std::regex_search(command, pattern)) {
      return false;
    }
  }
  return true;
}

std::string Sandbox::SanitizePath(const std::string& path) const {
  std::filesystem::path clean_path =
      std::filesystem::path(path).lexically_normal();
  if (clean_path.string().substr(0, 2) == "..") {
    throw std::runtime_error("Path traversal detected: " + path);
  }
  return clean_path.string();
}

bool Sandbox::ValidateFilePath(const std::string& path,
                               const std::string& /*workspace*/) {
  // Basic path traversal check
  std::filesystem::path clean = std::filesystem::path(path).lexically_normal();
  std::string path_str = clean.string();
  if (path_str.find("..") != std::string::npos) {
    return false;
  }
  return true;
}

bool Sandbox::ValidateShellCommand(const std::string& command) {
  // Block obviously dangerous commands
  static const std::vector<std::regex> dangerous_patterns = {
      std::regex(R"(\brm\s+-rf\s+/)", std::regex_constants::icase),
      std::regex(R"(\bmkfs\b)", std::regex_constants::icase),
      std::regex(R"(\bdd\s+if=)", std::regex_constants::icase),
  };

  for (const auto& pattern : dangerous_patterns) {
    if (std::regex_search(command, pattern)) {
      return false;
    }
  }
  return true;
}

void Sandbox::ApplyResourceLimits() {
#ifdef __linux__
  // Limit CPU time (30s soft, 60s hard)
  struct rlimit cpu_limit;
  cpu_limit.rlim_cur = 30;
  cpu_limit.rlim_max = 60;
  setrlimit(RLIMIT_CPU, &cpu_limit);

  // Limit virtual memory (256 MB soft, 512 MB hard)
  struct rlimit mem_limit;
  mem_limit.rlim_cur = 256ULL * 1024 * 1024;
  mem_limit.rlim_max = 512ULL * 1024 * 1024;
  setrlimit(RLIMIT_AS, &mem_limit);

  // Limit file size (64 MB soft, 128 MB hard)
  struct rlimit fsize_limit;
  fsize_limit.rlim_cur = 64ULL * 1024 * 1024;
  fsize_limit.rlim_max = 128ULL * 1024 * 1024;
  setrlimit(RLIMIT_FSIZE, &fsize_limit);

  // Limit child processes (32 soft, 64 hard)
  struct rlimit nproc_limit;
  nproc_limit.rlim_cur = 32;
  nproc_limit.rlim_max = 64;
  setrlimit(RLIMIT_NPROC, &nproc_limit);
#endif
  // macOS / Windows: no-op (macOS setrlimit semantics differ, not implemented)
}

}  // namespace quantclaw
