// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// Approval ask mode (compatible with OpenClaw tools.exec.ask)
enum class AskMode {
  kOff,     // No approval needed
  kOnMiss,  // Ask only if not in allowlist
  kAlways,  // Always require approval
};

AskMode AskModeFromString(const std::string& s);
std::string AskModeToString(AskMode m);

// Approval decision
enum class ApprovalDecision {
  kApproved,
  kDenied,
  kTimeout,  // Expired without decision
  kPending,  // Still waiting
};

std::string ApprovalDecisionToString(ApprovalDecision d);

// An exec approval request
struct ApprovalRequest {
  std::string id;
  std::string command;
  std::string cwd;
  std::string agent_id;
  std::string session_key;
  std::string security_note;
  std::chrono::steady_clock::time_point created_at;
  std::chrono::steady_clock::time_point expires_at;
};

// A resolved approval
struct ApprovalResolved {
  std::string id;
  ApprovalDecision decision = ApprovalDecision::kPending;
  std::string resolved_by;
  std::chrono::steady_clock::time_point resolved_at;
};

// Allowlist pattern matching for exec commands
class ExecAllowlist {
 public:
  // Add a glob pattern to the allowlist
  void AddPattern(const std::string& pattern);

  // Check if a command matches any allowlist pattern
  bool Matches(const std::string& command) const;

  // Load patterns from JSON array
  void LoadFromJson(const nlohmann::json& j);

  // Get all patterns
  const std::vector<std::string>& Patterns() const {
    return patterns_;
  }

 private:
  std::vector<std::string> patterns_;

  static bool glob_match(const std::string& pattern, const std::string& text);
};

// Exec approval configuration (from config tools.exec section)
struct ExecApprovalConfig {
  AskMode ask = AskMode::kOnMiss;
  int timeout_seconds = 120;  // Auto-expire after this
  ApprovalDecision timeout_fallback =
      ApprovalDecision::kDenied;  // What to do on timeout
  std::vector<std::string>
      allowlist;  // Glob patterns for auto-approved commands
  int approval_notice_ms =
      5000;  // Show "waiting for approval" notice after N ms

  static ExecApprovalConfig FromJson(const nlohmann::json& j);
};

// Callback for requesting approval from operator
using ApprovalCallback =
    std::function<ApprovalDecision(const ApprovalRequest&)>;

// Manages tool execution approvals
class ExecApprovalManager {
 public:
  explicit ExecApprovalManager(std::shared_ptr<spdlog::logger> logger);

  // Configure approval settings
  void Configure(const ExecApprovalConfig& config);

  // Set the approval callback (called when approval is needed)
  void SetApprovalHandler(ApprovalCallback handler);

  // Check if a command requires approval; if so, request it.
  // Returns the decision. Blocks until resolved or timeout.
  ApprovalDecision RequestApproval(const std::string& command,
                                   const std::string& cwd = "",
                                   const std::string& agent_id = "",
                                   const std::string& session_key = "");

  // Resolve a pending request (from operator UI / socket)
  bool Resolve(const std::string& request_id, ApprovalDecision decision,
               const std::string& resolved_by = "operator");

  // Get pending requests
  std::vector<ApprovalRequest> PendingRequests() const;

  // Get resolved history
  std::vector<ApprovalResolved> ResolvedHistory() const;

  // Prune expired requests
  void PruneExpired();

  // Get config
  const ExecApprovalConfig& GetConfig() const {
    return config_;
  }

 private:
  std::shared_ptr<spdlog::logger> logger_;
  ExecApprovalConfig config_;
  ExecAllowlist allowlist_;
  ApprovalCallback approval_handler_;

  mutable std::mutex mu_;
  std::unordered_map<std::string, ApprovalRequest> pending_;
  std::vector<ApprovalResolved> resolved_;

  std::string generate_request_id() const;
};

}  // namespace quantclaw
