// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// DM policy determines how direct messages are handled
enum class DmPolicy {
  kOpen,     // Accept all DMs (filtered by allowFrom if set)
  kPairing,  // Require pairing code before accepting DMs
};

DmPolicy DmPolicyFromString(const std::string& s);
std::string DmPolicyToString(DmPolicy p);

// Session DM scope determines session isolation per sender
enum class DmScope {
  kMain,                   // All DMs share the main session
  kPerPeer,                // Each sender gets their own session
  kPerChannelPeer,         // Each channel+sender gets their own session
  kPerAccountChannelPeer,  // Each account+channel+sender
};

DmScope DmScopeFromString(const std::string& s);
std::string DmScopeToString(DmScope s);

// Group activation mode
enum class GroupActivation {
  kMention,  // Only respond when @mentioned
  kAlways,   // Respond to all group messages
};

GroupActivation GroupActivationFromString(const std::string& s);

// Channel-specific policy configuration, parsed from channel config
struct ChannelPolicyConfig {
  DmPolicy dm_policy = DmPolicy::kOpen;
  DmScope dm_scope = DmScope::kPerChannelPeer;
  GroupActivation group_activation = GroupActivation::kMention;
  std::vector<std::string> allow_from;  // sender IDs allowed in open mode
  int group_chunk_size = 2000;          // max chars per group message chunk
  std::string bot_name;                 // for @mention detection

  static ChannelPolicyConfig FromJson(const nlohmann::json& j);
};

// Manages pairing state for channels using DmPolicy::kPairing
class PairingManager {
 public:
  explicit PairingManager(std::shared_ptr<spdlog::logger> logger);

  // Generate a pairing code for a channel
  std::string GenerateCode(const std::string& channel_id);

  // Verify and consume a pairing code. Returns true if valid.
  bool VerifyCode(const std::string& channel_id, const std::string& code,
                  const std::string& sender_id);

  // Check if a sender is already paired
  bool IsPaired(const std::string& channel_id,
                const std::string& sender_id) const;

  // Get all paired sender IDs for a channel
  std::vector<std::string> PairedSenders(const std::string& channel_id) const;

  // Unpair a sender
  void Unpair(const std::string& channel_id, const std::string& sender_id);

 private:
  std::shared_ptr<spdlog::logger> logger_;

  mutable std::mutex mu_;
  // channel_id → active pairing code
  std::unordered_map<std::string, std::string> codes_;
  // channel_id → set of paired sender IDs
  std::unordered_map<std::string, std::unordered_set<std::string>> paired_;
};

// Resolves session keys based on DmScope and message context
class SessionResolver {
 public:
  // Build a session key from message context
  static std::string ResolveSessionKey(DmScope scope,
                                       const std::string& agent_id,
                                       const std::string& channel_id,
                                       const std::string& sender_id,
                                       const std::string& account_id = "");

  // Check if a group message should activate the agent
  static bool
  ShouldActivateGroup(GroupActivation mode, const std::string& message,
                      const std::string& bot_name,
                      const std::vector<std::string>& mention_patterns = {});
};

}  // namespace quantclaw
