// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.security.scope_validator;

import std;
import nlohmann.json;

export namespace quantclaw {

// Validates tool call targets against accepted/restricted scope sets.
// Designed for bug bounty recon engagements where the model reasons freely
// but the harness enforces scope compliance at the execution gate.
class ScopeValidator {
 public:
  explicit ScopeValidator(std::shared_ptr<spdlog::logger> logger);

  // Load accepted and restricted target sets from recon config JSON.
  // Expected keys: "accepted_targets" (array<string>), "restricted_targets"
  // (array<string>). Supports domains, wildcards (*.example.com), CIDRs
  // (10.0.0.0/24), and bare IPs.
  void Configure(const nlohmann::json& recon_config);

  // Check if a single target (domain, IP, CIDR) is in scope.
  // Returns true if it matches an accepted target and does NOT match
  // any restricted target.
  bool IsTargetInScope(const std::string& target) const;

  // Validate a tool call's parameters for scope compliance.
  // Inspects known parameter keys (target, domain, host, url) and
  // extracts hostnames/IPs to validate.
  // Returns empty string if valid, otherwise an error description.
  std::string ValidateToolCall(const std::string& tool_name,
                               const nlohmann::json& params) const;

  // Whether scope enforcement is active (configured with at least one
  // accepted target).
  bool IsEnabled() const { return !accepted_targets_.empty(); }

  const std::vector<std::string>& AcceptedTargets() const {
    return accepted_targets_;
  }
  const std::vector<std::string>& RestrictedTargets() const {
    return restricted_targets_;
  }

  // Format scope sets as a human-readable string for injection into
  // the system prompt.
  std::string FormatAcceptedTargets() const;
  std::string FormatRestrictedTargets() const;

 private:
  // Check if target matches a pattern (domain exact, wildcard, CIDR).
  bool matches_pattern(const std::string& target,
                       const std::string& pattern) const;

  // Wildcard domain matching: *.example.com matches sub.example.com
  bool matches_wildcard(const std::string& target,
                        const std::string& pattern) const;

  // CIDR matching: 10.0.0.5 matches 10.0.0.0/24
  bool matches_cidr(const std::string& ip, const std::string& cidr) const;

  // Parse an IPv4 address to a 32-bit integer. Returns false on failure.
  static bool parse_ipv4(const std::string& ip, std::uint32_t& out);

  // Check if a string looks like an IPv4 address.
  static bool is_ipv4(const std::string& s);

  // Check if a string looks like a CIDR notation.
  static bool is_cidr(const std::string& s);

  // Extract hostname from a URL (strips scheme, port, path).
  static std::string extract_hostname(const std::string& url);

  // Extract target-like values from tool parameters.
  std::vector<std::string> extract_targets(const std::string& tool_name,
                                           const nlohmann::json& params) const;

  std::shared_ptr<spdlog::logger> logger_;
  std::vector<std::string> accepted_targets_;
  std::vector<std::string> restricted_targets_;

  // Known parameter keys that contain targets.
  static constexpr std::array<const char*, 5> kTargetParamKeys = {
      "target", "domain", "host", "url", "hostname"};
};

}  // namespace quantclaw
