// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/channels/channel_policy.hpp"

#include <algorithm>
#include <random>
#include <regex>
#include <sstream>

namespace quantclaw {

DmPolicy DmPolicyFromString(const std::string& s) {
  if (s == "pairing")
    return DmPolicy::kPairing;
  return DmPolicy::kOpen;
}

std::string DmPolicyToString(DmPolicy p) {
  switch (p) {
    case DmPolicy::kPairing:
      return "pairing";
    case DmPolicy::kOpen:
      return "open";
  }
  return "open";
}

DmScope DmScopeFromString(const std::string& s) {
  if (s == "main")
    return DmScope::kMain;
  if (s == "per-peer")
    return DmScope::kPerPeer;
  if (s == "per-channel-peer")
    return DmScope::kPerChannelPeer;
  if (s == "per-account-channel-peer")
    return DmScope::kPerAccountChannelPeer;
  return DmScope::kPerChannelPeer;
}

std::string DmScopeToString(DmScope s) {
  switch (s) {
    case DmScope::kMain:
      return "main";
    case DmScope::kPerPeer:
      return "per-peer";
    case DmScope::kPerChannelPeer:
      return "per-channel-peer";
    case DmScope::kPerAccountChannelPeer:
      return "per-account-channel-peer";
  }
  return "per-channel-peer";
}

GroupActivation GroupActivationFromString(const std::string& s) {
  if (s == "always")
    return GroupActivation::kAlways;
  return GroupActivation::kMention;
}

ChannelPolicyConfig ChannelPolicyConfig::FromJson(const nlohmann::json& j) {
  ChannelPolicyConfig c;
  c.dm_policy = DmPolicyFromString(j.value("dmPolicy", "open"));
  c.dm_scope = DmScopeFromString(j.value("dmScope", "per-channel-peer"));
  c.group_activation =
      GroupActivationFromString(j.value("groupActivation", "mention"));
  c.group_chunk_size = j.value("groupChunkSize", 2000);
  c.bot_name = j.value("botName", "");

  if (j.contains("allowFrom") && j["allowFrom"].is_array()) {
    for (const auto& id : j["allowFrom"]) {
      if (id.is_string())
        c.allow_from.push_back(id.get<std::string>());
    }
  }
  return c;
}

// --- PairingManager ---

PairingManager::PairingManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

std::string PairingManager::GenerateCode(const std::string& channel_id) {
  // Generate a 6-digit numeric code
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(100000, 999999);

  std::string code = std::to_string(dist(gen));

  std::lock_guard<std::mutex> lock(mu_);
  codes_[channel_id] = code;

  logger_->info("Generated pairing code for channel {}", channel_id);
  return code;
}

bool PairingManager::VerifyCode(const std::string& channel_id,
                                const std::string& code,
                                const std::string& sender_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = codes_.find(channel_id);
  if (it == codes_.end() || it->second != code) {
    return false;
  }

  // Code verified — pair the sender
  paired_[channel_id].insert(sender_id);
  codes_.erase(it);

  logger_->info("Paired sender {} on channel {}", sender_id, channel_id);
  return true;
}

bool PairingManager::IsPaired(const std::string& channel_id,
                              const std::string& sender_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = paired_.find(channel_id);
  if (it == paired_.end())
    return false;
  return it->second.count(sender_id) > 0;
}

std::vector<std::string>
PairingManager::PairedSenders(const std::string& channel_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = paired_.find(channel_id);
  if (it == paired_.end())
    return {};
  return {it->second.begin(), it->second.end()};
}

void PairingManager::Unpair(const std::string& channel_id,
                            const std::string& sender_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = paired_.find(channel_id);
  if (it != paired_.end()) {
    it->second.erase(sender_id);
  }
}

// --- SessionResolver ---

std::string SessionResolver::ResolveSessionKey(DmScope scope,
                                               const std::string& agent_id,
                                               const std::string& channel_id,
                                               const std::string& sender_id,
                                               const std::string& account_id) {
  // Format: agent:{agent_id}:{scope_key}
  switch (scope) {
    case DmScope::kMain:
      return "agent:" + agent_id + ":main";

    case DmScope::kPerPeer:
      return "agent:" + agent_id + ":peer:" + sender_id;

    case DmScope::kPerChannelPeer:
      return "agent:" + agent_id + ":" + channel_id + ":" + sender_id;

    case DmScope::kPerAccountChannelPeer: {
      std::string acct = account_id.empty() ? "default" : account_id;
      return "agent:" + agent_id + ":" + acct + ":" + channel_id + ":" +
             sender_id;
    }
  }
  return "agent:" + agent_id + ":main";
}

bool SessionResolver::ShouldActivateGroup(
    GroupActivation mode, const std::string& message,
    const std::string& bot_name,
    const std::vector<std::string>& mention_patterns) {
  if (mode == GroupActivation::kAlways)
    return true;

  // Check for @mention
  if (!bot_name.empty()) {
    // Case-insensitive check for @botname
    std::string lower_msg = message;
    std::string lower_name = bot_name;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(),
                   ::tolower);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   ::tolower);

    if (lower_msg.find("@" + lower_name) != std::string::npos) {
      return true;
    }
  }

  // Check custom mention patterns (regex)
  for (const auto& pattern : mention_patterns) {
    try {
      std::regex re(pattern, std::regex::icase);
      if (std::regex_search(message, re))
        return true;
    } catch (const std::exception&) {
      // Invalid regex pattern, skip
    }
  }

  return false;
}

}  // namespace quantclaw
