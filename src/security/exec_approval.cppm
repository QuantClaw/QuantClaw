// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.security.exec_approval;

import std;
import nlohmann.json;

namespace spdlog {
class logger;
}

export namespace quantclaw {

enum class AskMode {
  kOff,
  kOnMiss,
  kAlways,
};

AskMode AskModeFromString(const std::string& s);
std::string AskModeToString(AskMode m);

enum class ApprovalDecision {
  kApproved,
  kDenied,
  kTimeout,
  kPending,
};

std::string ApprovalDecisionToString(ApprovalDecision d);

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

struct ApprovalResolved {
  std::string id;
  ApprovalDecision decision = ApprovalDecision::kPending;
  std::string resolved_by;
  std::chrono::steady_clock::time_point resolved_at;
};

class ExecAllowlist {
 public:
  void AddPattern(const std::string& pattern);
  bool Matches(const std::string& command) const;
  void LoadFromJson(const nlohmann::json& j);
  const std::vector<std::string>& Patterns() const { return patterns_; }

 private:
  std::vector<std::string> patterns_;

  static bool glob_match(const std::string& pattern, const std::string& text);
};

struct ExecApprovalConfig {
  AskMode ask = AskMode::kOnMiss;
  int timeout_seconds = 120;
  ApprovalDecision timeout_fallback = ApprovalDecision::kDenied;
  std::vector<std::string> allowlist;
  int approval_notice_ms = 5000;

  static ExecApprovalConfig FromJson(const nlohmann::json& j);
};

using ApprovalCallback = std::function<ApprovalDecision(const ApprovalRequest&)>;

class ExecApprovalManager {
 public:
  explicit ExecApprovalManager(std::shared_ptr<spdlog::logger> logger);

  void Configure(const ExecApprovalConfig& config);
  void SetApprovalHandler(ApprovalCallback handler);
  ApprovalDecision RequestApproval(const std::string& command,
                                   const std::string& cwd = "",
                                   const std::string& agent_id = "",
                                   const std::string& session_key = "");
  bool Resolve(const std::string& request_id, ApprovalDecision decision,
               const std::string& resolved_by = "operator");
  std::vector<ApprovalRequest> PendingRequests() const;
  std::vector<ApprovalResolved> ResolvedHistory() const;
  void PruneExpired();
  const ExecApprovalConfig& GetConfig() const { return config_; }

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