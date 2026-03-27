// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import quantclaw.security.sandbox;

import std;

import <spdlog/spdlog.h>;
#ifdef __linux__
import <sys/resource.h>;
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
                               const std::string& workspace) {
  namespace fs = std::filesystem;
  std::error_code ec;

  // Resolve workspace and path to canonical form so symlinks are resolved
  // for the existing prefix and ".." segments are collapsed.
  fs::path ws_abs = fs::weakly_canonical(workspace, ec);
  if (ec)
    return false;

  // Resolve relative paths against the workspace root (not CWD).
  fs::path input_path(path);
  if (input_path.is_relative()) {
    input_path = ws_abs / input_path;
  }
  fs::path path_abs = fs::weakly_canonical(input_path, ec);
  if (ec)
    return false;

  // Basic traversal check on the normalized string.
  std::string path_str = path_abs.string();
  if (path_str.find("..") != std::string::npos) {
    return false;
  }

  // Ensure the resolved path is inside the workspace using component-level
  // iteration so that "/tmp" does not match "/tmp2/...".
  auto ws_it = ws_abs.begin();
  auto path_it = path_abs.begin();
  for (; ws_it != ws_abs.end(); ++ws_it, ++path_it) {
    if (path_it == path_abs.end())
      return false;  // path is shorter than workspace
#ifdef _WIN32
    // Case-insensitive comparison on Windows.
    auto to_lower = [](std::string s) {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return s;
    };
    if (to_lower(ws_it->string()) != to_lower(path_it->string()))
      return false;
#else
    if (*ws_it != *path_it)
      return false;
#endif
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
  // Resource limits are now applied inside exec_capture() on the child
  // process (via fork + setrlimit before exec on Linux). Calling setrlimit
  // on the host process would permanently cap the gateway itself.
  // This function is intentionally a no-op; the actual enforcement lives
  // in process_unix.cpp.
}

}  // namespace quantclaw
