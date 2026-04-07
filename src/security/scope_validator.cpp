// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>
#include <cstdlib>

module quantclaw.security.scope_validator;

import std;
import nlohmann.json;

namespace quantclaw {

ScopeValidator::ScopeValidator(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void ScopeValidator::Configure(const nlohmann::json& recon_config) {
  accepted_targets_.clear();
  restricted_targets_.clear();

  if (recon_config.contains("accepted_targets") &&
      recon_config["accepted_targets"].is_array()) {
    for (const auto& t : recon_config["accepted_targets"]) {
      if (t.is_string()) {
        accepted_targets_.push_back(t.get<std::string>());
      }
    }
  }

  if (recon_config.contains("restricted_targets") &&
      recon_config["restricted_targets"].is_array()) {
    for (const auto& t : recon_config["restricted_targets"]) {
      if (t.is_string()) {
        restricted_targets_.push_back(t.get<std::string>());
      }
    }
  }

  logger_->info("ScopeValidator configured: {} accepted, {} restricted",
                accepted_targets_.size(), restricted_targets_.size());
}

bool ScopeValidator::IsTargetInScope(const std::string& target) const {
  if (accepted_targets_.empty())
    return false;

  std::string lower_target = target;
  std::transform(lower_target.begin(), lower_target.end(),
                 lower_target.begin(), ::tolower);

  // First check restricted — restricted always wins.
  for (const auto& restricted : restricted_targets_) {
    if (matches_pattern(lower_target, restricted)) {
      logger_->warn("Target '{}' matches restricted scope: {}", target,
                    restricted);
      return false;
    }
  }

  // Then check accepted.
  for (const auto& accepted : accepted_targets_) {
    if (matches_pattern(lower_target, accepted)) {
      return true;
    }
  }

  logger_->warn("Target '{}' not in accepted scope", target);
  return false;
}

std::string ScopeValidator::ValidateToolCall(
    const std::string& tool_name, const nlohmann::json& params) const {
  if (!IsEnabled())
    return "";

  auto targets = extract_targets(tool_name, params);
  if (targets.empty()) {
    // Tools without identifiable targets pass through — the scope gate
    // only blocks when it can identify an out-of-scope target.
    return "";
  }

  for (const auto& target : targets) {
    if (!IsTargetInScope(target)) {
      std::string error = "Target '" + target +
                          "' is out of scope for tool '" + tool_name +
                          "'. Accepted: " + FormatAcceptedTargets();
      if (!restricted_targets_.empty()) {
        error += " | Restricted: " + FormatRestrictedTargets();
      }
      logger_->error("SCOPE VIOLATION: {}", error);
      return error;
    }
  }

  return "";
}

std::string ScopeValidator::FormatAcceptedTargets() const {
  std::ostringstream oss;
  for (std::size_t i = 0; i < accepted_targets_.size(); ++i) {
    if (i > 0)
      oss << ", ";
    oss << accepted_targets_[i];
  }
  return oss.str();
}

std::string ScopeValidator::FormatRestrictedTargets() const {
  std::ostringstream oss;
  for (std::size_t i = 0; i < restricted_targets_.size(); ++i) {
    if (i > 0)
      oss << ", ";
    oss << restricted_targets_[i];
  }
  return oss.str();
}

// ---------------------------------------------------------------------------
// Pattern matching
// ---------------------------------------------------------------------------

bool ScopeValidator::matches_pattern(const std::string& target,
                                     const std::string& pattern) const {
  std::string lower_pattern = pattern;
  std::transform(lower_pattern.begin(), lower_pattern.end(),
                 lower_pattern.begin(), ::tolower);

  // Exact match.
  if (target == lower_pattern)
    return true;

  // Wildcard domain match: *.example.com
  if (lower_pattern.size() > 2 && lower_pattern[0] == '*' &&
      lower_pattern[1] == '.') {
    return matches_wildcard(target, lower_pattern);
  }

  // CIDR match: 10.0.0.0/24
  if (is_cidr(lower_pattern) && is_ipv4(target)) {
    return matches_cidr(target, lower_pattern);
  }

  return false;
}

bool ScopeValidator::matches_wildcard(const std::string& target,
                                      const std::string& pattern) const {
  // pattern is "*.example.com" — match any subdomain of example.com,
  // including the bare domain itself.
  std::string suffix = pattern.substr(1);  // ".example.com"
  std::string bare = pattern.substr(2);    // "example.com"

  // Bare domain match.
  if (target == bare)
    return true;

  // Subdomain match: target ends with ".example.com".
  if (target.size() > suffix.size() &&
      target.compare(target.size() - suffix.size(), suffix.size(), suffix) ==
          0) {
    return true;
  }

  return false;
}

bool ScopeValidator::matches_cidr(const std::string& ip,
                                  const std::string& cidr) const {
  auto slash_pos = cidr.find('/');
  if (slash_pos == std::string::npos)
    return false;

  std::string network_str = cidr.substr(0, slash_pos);
  int prefix_len = 0;
  try {
    prefix_len = std::stoi(cidr.substr(slash_pos + 1));
  } catch (...) {
    return false;
  }

  if (prefix_len < 0 || prefix_len > 32)
    return false;

  std::uint32_t ip_addr = 0;
  std::uint32_t network_addr = 0;
  if (!parse_ipv4(ip, ip_addr) || !parse_ipv4(network_str, network_addr))
    return false;

  std::uint32_t mask =
      prefix_len == 0 ? 0 : (~std::uint32_t{0}) << (32 - prefix_len);
  return (ip_addr & mask) == (network_addr & mask);
}

bool ScopeValidator::parse_ipv4(const std::string& ip, std::uint32_t& out) {
  std::uint32_t parts[4];
  int count = 0;
  std::istringstream stream(ip);
  std::string token;

  while (std::getline(stream, token, '.') && count < 4) {
    try {
      auto val = std::stoul(token);
      if (val > 255)
        return false;
      parts[count++] = static_cast<std::uint32_t>(val);
    } catch (...) {
      return false;
    }
  }

  if (count != 4)
    return false;

  out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
  return true;
}

bool ScopeValidator::is_ipv4(const std::string& s) {
  int dots = 0;
  for (char c : s) {
    if (c == '.')
      dots++;
    else if (!std::isdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return dots == 3;
}

bool ScopeValidator::is_cidr(const std::string& s) {
  auto slash = s.find('/');
  if (slash == std::string::npos)
    return false;
  return is_ipv4(s.substr(0, slash));
}

std::string ScopeValidator::extract_hostname(const std::string& url) {
  std::string s = url;

  // Strip scheme.
  auto scheme_end = s.find("://");
  if (scheme_end != std::string::npos) {
    s = s.substr(scheme_end + 3);
  }

  // Strip userinfo.
  auto at = s.find('@');
  if (at != std::string::npos) {
    s = s.substr(at + 1);
  }

  // Strip port.
  auto colon = s.find(':');
  if (colon != std::string::npos) {
    s = s.substr(0, colon);
  }

  // Strip path.
  auto slash = s.find('/');
  if (slash != std::string::npos) {
    s = s.substr(0, slash);
  }

  // Strip query.
  auto question = s.find('?');
  if (question != std::string::npos) {
    s = s.substr(0, question);
  }

  return s;
}

std::vector<std::string> ScopeValidator::extract_targets(
    const std::string& /*tool_name*/, const nlohmann::json& params) const {
  std::vector<std::string> targets;

  if (!params.is_object())
    return targets;

  for (const auto& key : kTargetParamKeys) {
    if (params.contains(key) && params[key].is_string()) {
      std::string val = params[key].get<std::string>();
      if (!val.empty()) {
        // If it looks like a URL, extract the hostname.
        if (val.find("://") != std::string::npos ||
            val.find('/') != std::string::npos) {
          auto host = extract_hostname(val);
          if (!host.empty())
            targets.push_back(host);
        } else {
          targets.push_back(val);
        }
      }
    }
    // Also handle array values (e.g., multiple targets).
    if (params.contains(key) && params[key].is_array()) {
      for (const auto& item : params[key]) {
        if (item.is_string()) {
          std::string val = item.get<std::string>();
          if (val.find("://") != std::string::npos) {
            auto host = extract_hostname(val);
            if (!host.empty())
              targets.push_back(host);
          } else if (!val.empty()) {
            targets.push_back(val);
          }
        }
      }
    }
  }

  return targets;
}

}  // namespace quantclaw
