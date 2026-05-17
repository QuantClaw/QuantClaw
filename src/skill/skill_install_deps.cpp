// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/skill/skill_install_deps.hpp"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <sys/stat.h>
#endif

#include <spdlog/spdlog.h>

#include "quantclaw/platform/process.hpp"

namespace quantclaw {

SkillInstallDeps::SkillInstallDeps(
  std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {
  full_loader_ = std::make_shared<SkillLoaderFull>(logger_); 
  logger_->info("SkillInstallDeps initialized");
}

// ---------------------------------------------------------------------------
// Input validation: whitelist package refs to prevent shell injection
// ---------------------------------------------------------------------------

bool SkillInstallDeps::IsValidPackageRef(const std::string& ref,
                                         const std::string& method) {
  if (ref.empty() || ref.size() > 512) return false;

  // Baseline: alphanumeric, dash, underscore, dot
  auto is_base = [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  };

  for (char ch : ref) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (is_base(c)) continue;
    // Method-specific allowances
    if (method == "go" && (c == '/' || c == '@' || c == '.')) continue;
    if (method == "node" && (c == '@' || c == '/')) continue;
    if (method == "uv" && (c == '=' || c == '<' || c == '>' || c == '[' || c == ']')) continue;
    if (method == "apt" && (c == '+' || c == ':')) continue;
    if (method == "brew" && (c == '@')) continue;
    if (method == "download" && (c == '/' || c == ':' || c == '?' || c == '&' || c == '=' || c == '%' || c == '+' || c == '_' || c == '-' || c == '.')) continue;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Safe command execution via fork+exec (no shell interpretation)
// ---------------------------------------------------------------------------

int SkillInstallDeps::RunCommand(
    const std::vector<std::string>& args) const {
  if (args.empty()) return -1;

  platform::ProcessId pid = platform::spawn_process(args);
  if (pid == platform::kInvalidPid) {
    logger_->error("RunCommand: failed to spawn '{}'", args[0]);
    return -1;
  }

  return platform::wait_process(pid);
}

// ---------------------------------------------------------------------------
// Skill dependency installation
// ---------------------------------------------------------------------------

bool SkillInstallDeps::InstallSkillDependence(
    const SkillFullData& skill) {
  if (!full_loader_->CheckSkillGating(skill)) {
    logger_->debug("InstallSkillDependence: gating failed for '{}'",
                   skill.mini.name);
    return false;
  }

  if (skill.extra.installs.empty()) {
    logger_->info("Skill '{}' has no install instructions", skill.mini.name);
    return true;
  }

  bool all_ok = true;
  for (const auto& inst : skill.extra.installs) {
    std::string eff_binary = inst.EffectiveBinary();
    std::string eff_method = inst.EffectiveMethod();
    std::string eff_formula = inst.EffectiveFormula();

    // Skip if binary already available
    if (!eff_binary.empty() && IsBinaryAvailable(eff_binary)) {
      logger_->debug("Skill '{}': {} already installed", skill.mini.name,
                     eff_binary);
      continue;
    }

    // Validate inputs against whitelist to prevent injection
    if (!IsValidPackageRef(eff_formula, eff_method)) {
      logger_->error("Skill '{}': invalid formula '{}' for method '{}'",
                     skill.mini.name, eff_formula, eff_method);
      all_ok = false;
      continue;
    }
    if (!eff_binary.empty() && !IsValidPackageRef(eff_binary, "binary")) {
      logger_->error("Skill '{}': invalid binary name '{}'",
                     skill.mini.name, eff_binary);
      all_ok = false;
      continue;
    }

    int ret = -1;
    if (eff_method == "node") {
      ret = RunCommand({"npm", "install", "-g", eff_formula});
    } else if (eff_method == "go") {
      ret = RunCommand({"go", "install", eff_formula});
    } else if (eff_method == "uv") {
      ret = RunCommand({"uv", "pip", "install", eff_formula});
    } else if (eff_method == "apt") {
      ret = RunCommand({"sudo", "apt-get", "install", "-y", eff_formula});
    } else if (eff_method == "brew") {
      ret = RunCommand({"brew", "install", eff_formula});
    } else if (eff_method == "download") {
      std::string bin_dir = platform::home_directory() + "/.quantclaw/bin";
      std::filesystem::create_directories(bin_dir);
      // Sanitize binary name: extract basename to prevent path traversal
      std::string safe_name = eff_binary.empty()
          ? "downloaded"
          : std::filesystem::path(eff_binary).filename().string();
      if (safe_name.empty() || safe_name == "." || safe_name == "..") {
        safe_name = "downloaded";
      }
      std::string dest = bin_dir + "/" + safe_name;
      // Step 1: download via fork+exec (no shell)
      ret = RunCommand({"curl", "-fsSL", "-o", dest, eff_formula});
      // Step 2: make executable via native chmod() (no child process)
      if (ret == 0) {
        if (::chmod(dest.c_str(), 0755) != 0) {
          logger_->error("chmod('{}') failed: {}", dest, std::strerror(errno));
          ret = -1;
        }
      }
    } else {
      logger_->warn("Skill '{}': unknown install method '{}'", skill.mini.name,
                    eff_method);
      continue;
    }

    if (ret != 0) {
      logger_->error("Skill '{}' install failed (exit {})", skill.mini.name, ret);
      all_ok = false;
    }
  }
  return all_ok;
}

bool SkillInstallDeps::InstallSkillDependenceByName(
    const std::string& skill_name,
    const std::vector<SkillMetadata>& metas) {
  SkillMetadata target_meta;
  bool found = false;
  for (const auto& meta : metas) {
    if (meta.mini.name == skill_name) {
      target_meta = meta;
      found = true;
      break;
    }
  }

  if (!found) {
    logger_->warn("InstallSkillDependenceByName: skill '{}' not found in metas",
                  skill_name);
    return false;
  }

  // Load full data from the metadata, then install
  try {
    auto full_data = full_loader_->LoaderOneFullData(target_meta);
    return InstallSkillDependence(full_data);
  } catch (const std::exception& e) {
    logger_->error("InstallSkillDependenceByName: failed to load full data for '{}': {}",
                   skill_name, e.what());
    return false;
  }
}

int SkillInstallDeps::InstallAllSkillDependences(
    const std::vector<SkillMetadata>& metas) {
  int installed = 0;
  for (const auto& meta : metas) {
    try {
      auto full_data = full_loader_->LoaderOneFullData(meta);

      // Skip skills that don't need installation
      if (full_data.extra.installs.empty()) {
        continue;
      }

      // Check gating before attempting install
      if (!full_loader_->CheckSkillGating(full_data)) {
        logger_->debug("InstallAllSkillDependences: skipping '{}' (gating failed)",
                       full_data.mini.name);
        continue;
      }

      if (InstallSkillDependence(full_data)) {
        ++installed;
      }
    } catch (const std::exception& e) {
      logger_->error("InstallAllSkillDependences: failed for '{}': {}",
                     meta.mini.name, e.what());
    }
  }

  logger_->info("InstallAllSkillDependences: {}/{} skills installed", installed,
                metas.size());
  return installed;
}

bool SkillInstallDeps::IsBinaryAvailable(
    const std::string& binary_name) const {
  // Whitelist validation: only allow [a-zA-Z0-9_-] in binary names.
  // Prevents shell injection via metacharacters in skill config files.
  if (binary_name.empty() || binary_name.size() > 255) return false;
  for (char c : binary_name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' &&
        c != '_') {
      return false;
    }
  }

#ifdef _WIN32
  // Windows: search PATH directories for the binary (no shell invocation).
  const char* path_env = std::getenv("PATH");
  if (!path_env) return false;
  std::istringstream iss(path_env);
  std::string dir;
  while (std::getline(iss, dir, ';')) {
    if (dir.empty()) continue;
    auto candidate = std::filesystem::path(dir) / (binary_name + ".exe");
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) return true;
    // Also check without .exe (e.g. batch scripts)
    auto candidate_no_ext = std::filesystem::path(dir) / binary_name;
    if (std::filesystem::exists(candidate_no_ext, ec) && !ec) return true;
  }
  return false;
#else
  // POSIX: search PATH directories using access(X_OK) (no shell invocation).
  const char* path_env = std::getenv("PATH");
  if (!path_env) return false;
  std::istringstream iss(path_env);
  std::string dir;
  while (std::getline(iss, dir, ':')) {
    if (dir.empty()) continue;
    auto full_path = std::filesystem::path(dir) / binary_name;
    if (access(full_path.c_str(), X_OK) == 0) return true;
  }
  return false;
#endif
}

}  // namespace quantclaw
