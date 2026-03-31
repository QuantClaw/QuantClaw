// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.channels.channel_policy;

import std;
import nlohmann.json;

export namespace quantclaw {

enum class DmPolicy {
  kOpen,
  kPairing,
};

DmPolicy DmPolicyFromString(const std::string& s);
std::string DmPolicyToString(DmPolicy p);

enum class DmScope {
  kMain,
  kPerPeer,
  kPerChannelPeer,
  kPerAccountChannelPeer,
};

DmScope DmScopeFromString(const std::string& s);
std::string DmScopeToString(DmScope s);

enum class GroupActivation {
  kMention,
  kAlways,
};

GroupActivation GroupActivationFromString(const std::string& s);

struct ChannelPolicyConfig {
  DmPolicy dm_policy = DmPolicy::kOpen;
  DmScope dm_scope = DmScope::kPerChannelPeer;
  GroupActivation group_activation = GroupActivation::kMention;
  std::vector<std::string> allow_from;
  int group_chunk_size = 2000;
  std::string bot_name;

  static ChannelPolicyConfig FromJson(const nlohmann::json& j);
};

class PairingManager {
 public:
  explicit PairingManager(std::shared_ptr<spdlog::logger> logger);

  std::string GenerateCode(const std::string& channel_id);
  bool VerifyCode(const std::string& channel_id, const std::string& code,
                  const std::string& sender_id);
  bool IsPaired(const std::string& channel_id,
                const std::string& sender_id) const;
  std::vector<std::string> PairedSenders(const std::string& channel_id) const;
  void Unpair(const std::string& channel_id, const std::string& sender_id);

 private:
  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::string> codes_;
  std::unordered_map<std::string, std::unordered_set<std::string>> paired_;
};

class SessionResolver {
 public:
  static std::string ResolveSessionKey(DmScope scope,
                                       const std::string& agent_id,
                                       const std::string& channel_id,
                                       const std::string& sender_id,
                                       const std::string& account_id = "");

  static bool ShouldActivateGroup(
      GroupActivation mode, const std::string& message,
      const std::string& bot_name,
      const std::vector<std::string>& mention_patterns = {});
};

}  // namespace quantclaw