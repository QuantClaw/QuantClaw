// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/exec_approval.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace quantclaw {

// --- AskMode ---

AskMode AskModeFromString(const std::string& s) {
  if (s == "off")
    return AskMode::kOff;
  if (s == "always")
    return AskMode::kAlways;
  return AskMode::kOnMiss;  // default
}

std::string AskModeToString(AskMode m) {
  switch (m) {
    case AskMode::kOff:
      return "off";
    case AskMode::kAlways:
      return "always";
    case AskMode::kOnMiss:
      return "on-miss";
  }
  return "on-miss";
}

// --- ApprovalDecision ---

std::string ApprovalDecisionToString(ApprovalDecision d) {
  switch (d) {
    case ApprovalDecision::kApproved:
      return "approved";
    case ApprovalDecision::kDenied:
      return "denied";
    case ApprovalDecision::kTimeout:
      return "timeout";
    case ApprovalDecision::kPending:
      return "pending";
  }
  return "pending";
}

// --- ExecAllowlist ---

void ExecAllowlist::AddPattern(const std::string& pattern) {
  patterns_.push_back(pattern);
}

bool ExecAllowlist::Matches(const std::string& command) const {
  for (const auto& pattern : patterns_) {
    if (glob_match(pattern, command))
      return true;
  }
  return false;
}

void ExecAllowlist::LoadFromJson(const nlohmann::json& j) {
  patterns_.clear();
  if (j.is_array()) {
    for (const auto& item : j) {
      if (item.is_string()) {
        patterns_.push_back(item.get<std::string>());
      }
    }
  }
}

bool ExecAllowlist::glob_match(const std::string& pattern,
                               const std::string& text) {
  // Simple glob matching: * matches anything, ? matches single char
  size_t pi = 0, ti = 0;
  size_t star_p = std::string::npos, star_t = 0;

  while (ti < text.size()) {
    if (pi < pattern.size() &&
        (pattern[pi] == text[ti] || pattern[pi] == '?')) {
      ++pi;
      ++ti;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star_p = pi;
      star_t = ti;
      ++pi;
    } else if (star_p != std::string::npos) {
      pi = star_p + 1;
      ++star_t;
      ti = star_t;
    } else {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*')
    ++pi;
  return pi == pattern.size();
}

// --- ExecApprovalConfig ---

ExecApprovalConfig ExecApprovalConfig::FromJson(const nlohmann::json& j) {
  ExecApprovalConfig c;
  if (j.contains("ask") && j["ask"].is_string()) {
    c.ask = AskModeFromString(j["ask"].get<std::string>());
  }
  c.timeout_seconds = j.value("approvalTimeout", 120);
  if (j.contains("askFallback") && j["askFallback"].is_string()) {
    auto fb = j["askFallback"].get<std::string>();
    c.timeout_fallback = (fb == "approve") ? ApprovalDecision::kApproved
                                           : ApprovalDecision::kDenied;
  }
  if (j.contains("allowlist")) {
    auto& al = j["allowlist"];
    if (al.is_array()) {
      for (const auto& item : al) {
        if (item.is_string())
          c.allowlist.push_back(item.get<std::string>());
      }
    }
  }
  c.approval_notice_ms = j.value("approvalRunningNoticeMs", 5000);
  return c;
}

// --- ExecApprovalManager ---

ExecApprovalManager::ExecApprovalManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void ExecApprovalManager::Configure(const ExecApprovalConfig& config) {
  std::lock_guard<std::mutex> lock(mu_);
  config_ = config;
  allowlist_ = ExecAllowlist{};
  for (const auto& p : config_.allowlist) {
    allowlist_.AddPattern(p);
  }
}

void ExecApprovalManager::SetApprovalHandler(ApprovalCallback handler) {
  std::lock_guard<std::mutex> lock(mu_);
  approval_handler_ = std::move(handler);
}

ApprovalDecision ExecApprovalManager::RequestApproval(
    const std::string& command, const std::string& cwd,
    const std::string& agent_id, const std::string& session_key) {
  // Check ask mode
  if (config_.ask == AskMode::kOff) {
    return ApprovalDecision::kApproved;
  }

  // Check allowlist for on-miss mode
  if (config_.ask == AskMode::kOnMiss && allowlist_.Matches(command)) {
    logger_->debug("Command '{}' matches allowlist, auto-approved", command);
    return ApprovalDecision::kApproved;
  }

  // Need approval — create request
  auto now = std::chrono::steady_clock::now();
  ApprovalRequest req;
  req.id = generate_request_id();
  req.command = command;
  req.cwd = cwd;
  req.agent_id = agent_id;
  req.session_key = session_key;
  req.created_at = now;
  req.expires_at = now + std::chrono::seconds(config_.timeout_seconds);

  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_[req.id] = req;
  }

  logger_->info("Approval requested for command: {} (id={})", command, req.id);

  // If there's a handler, call it synchronously
  ApprovalDecision decision = ApprovalDecision::kPending;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (approval_handler_) {
      decision = approval_handler_(req);
    }
  }

  // If no handler or handler returned pending, apply timeout fallback
  if (decision == ApprovalDecision::kPending) {
    decision = config_.timeout_fallback;
    logger_->info("No approval handler or pending, falling back to: {}",
                  ApprovalDecisionToString(decision));
  }

  // Resolve
  Resolve(req.id, decision, "auto");
  return decision;
}

bool ExecApprovalManager::Resolve(const std::string& request_id,
                                  ApprovalDecision decision,
                                  const std::string& resolved_by) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = pending_.find(request_id);
  if (it == pending_.end())
    return false;

  ApprovalResolved res;
  res.id = request_id;
  res.decision = decision;
  res.resolved_by = resolved_by;
  res.resolved_at = std::chrono::steady_clock::now();
  resolved_.push_back(res);
  pending_.erase(it);

  logger_->info("Approval {} resolved: {} by {}", request_id,
                ApprovalDecisionToString(decision), resolved_by);
  return true;
}

std::vector<ApprovalRequest> ExecApprovalManager::PendingRequests() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<ApprovalRequest> result;
  for (const auto& [id, req] : pending_) {
    result.push_back(req);
  }
  return result;
}

std::vector<ApprovalResolved> ExecApprovalManager::ResolvedHistory() const {
  std::lock_guard<std::mutex> lock(mu_);
  return resolved_;
}

void ExecApprovalManager::PruneExpired() {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();
  std::vector<std::string> expired;
  for (const auto& [id, req] : pending_) {
    if (now >= req.expires_at) {
      expired.push_back(id);
    }
  }
  for (const auto& id : expired) {
    ApprovalResolved res;
    res.id = id;
    res.decision = ApprovalDecision::kTimeout;
    res.resolved_by = "system";
    res.resolved_at = now;
    resolved_.push_back(res);
    pending_.erase(id);
    logger_->info("Approval {} expired", id);
  }
}

std::string ExecApprovalManager::generate_request_id() const {
  thread_local static std::mt19937 gen(std::random_device{}());
  thread_local static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << "req_" << std::hex << dist(gen);
  return ss.str();
}

}  // namespace quantclaw
