// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/channels/channel_policy.hpp"

#include <gtest/gtest.h>

static std::shared_ptr<spdlog::logger> make_null_logger() {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>("test", null_sink);
}

// --- DmPolicy / DmScope / GroupActivation enums ---

TEST(ChannelPolicyEnumsTest, DmPolicyFromString) {
  EXPECT_EQ(quantclaw::DmPolicyFromString("open"), quantclaw::DmPolicy::kOpen);
  EXPECT_EQ(quantclaw::DmPolicyFromString("pairing"),
            quantclaw::DmPolicy::kPairing);
  EXPECT_EQ(quantclaw::DmPolicyFromString("unknown"),
            quantclaw::DmPolicy::kOpen);
}

TEST(ChannelPolicyEnumsTest, DmPolicyToString) {
  EXPECT_EQ(quantclaw::DmPolicyToString(quantclaw::DmPolicy::kOpen), "open");
  EXPECT_EQ(quantclaw::DmPolicyToString(quantclaw::DmPolicy::kPairing),
            "pairing");
}

TEST(ChannelPolicyEnumsTest, DmScopeFromString) {
  EXPECT_EQ(quantclaw::DmScopeFromString("main"), quantclaw::DmScope::kMain);
  EXPECT_EQ(quantclaw::DmScopeFromString("per-peer"),
            quantclaw::DmScope::kPerPeer);
  EXPECT_EQ(quantclaw::DmScopeFromString("per-channel-peer"),
            quantclaw::DmScope::kPerChannelPeer);
  EXPECT_EQ(quantclaw::DmScopeFromString("per-account-channel-peer"),
            quantclaw::DmScope::kPerAccountChannelPeer);
  EXPECT_EQ(quantclaw::DmScopeFromString("garbage"),
            quantclaw::DmScope::kPerChannelPeer);
}

TEST(ChannelPolicyEnumsTest, GroupActivationFromString) {
  EXPECT_EQ(quantclaw::GroupActivationFromString("always"),
            quantclaw::GroupActivation::kAlways);
  EXPECT_EQ(quantclaw::GroupActivationFromString("mention"),
            quantclaw::GroupActivation::kMention);
  EXPECT_EQ(quantclaw::GroupActivationFromString("other"),
            quantclaw::GroupActivation::kMention);
}

// --- ChannelPolicyConfig ---

TEST(ChannelPolicyConfigTest, FromJsonDefaults) {
  nlohmann::json j = nlohmann::json::object();
  auto c = quantclaw::ChannelPolicyConfig::FromJson(j);
  EXPECT_EQ(c.dm_policy, quantclaw::DmPolicy::kOpen);
  EXPECT_EQ(c.dm_scope, quantclaw::DmScope::kPerChannelPeer);
  EXPECT_EQ(c.group_activation, quantclaw::GroupActivation::kMention);
  EXPECT_EQ(c.group_chunk_size, 2000);
  EXPECT_TRUE(c.allow_from.empty());
}

TEST(ChannelPolicyConfigTest, FromJsonFull) {
  nlohmann::json j = {
      {"dmPolicy", "pairing"},       {"dmScope", "per-peer"},
      {"groupActivation", "always"}, {"groupChunkSize", 3000},
      {"botName", "MyBot"},          {"allowFrom", {"user1", "user2"}},
  };
  auto c = quantclaw::ChannelPolicyConfig::FromJson(j);
  EXPECT_EQ(c.dm_policy, quantclaw::DmPolicy::kPairing);
  EXPECT_EQ(c.dm_scope, quantclaw::DmScope::kPerPeer);
  EXPECT_EQ(c.group_activation, quantclaw::GroupActivation::kAlways);
  EXPECT_EQ(c.group_chunk_size, 3000);
  EXPECT_EQ(c.bot_name, "MyBot");
  ASSERT_EQ(c.allow_from.size(), 2);
  EXPECT_EQ(c.allow_from[0], "user1");
}

// --- PairingManager ---

TEST(PairingManagerTest, GenerateAndVerifyCode) {
  auto logger = make_null_logger();
  quantclaw::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  EXPECT_EQ(code.size(), 6);

  EXPECT_FALSE(pm.IsPaired("discord", "user123"));
  EXPECT_TRUE(pm.VerifyCode("discord", code, "user123"));
  EXPECT_TRUE(pm.IsPaired("discord", "user123"));
}

TEST(PairingManagerTest, WrongCodeFails) {
  auto logger = make_null_logger();
  quantclaw::PairingManager pm(logger);

  pm.GenerateCode("telegram");
  EXPECT_FALSE(pm.VerifyCode("telegram", "000000", "user1"));
  EXPECT_FALSE(pm.IsPaired("telegram", "user1"));
}

TEST(PairingManagerTest, CodeConsumedAfterUse) {
  auto logger = make_null_logger();
  quantclaw::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  EXPECT_TRUE(pm.VerifyCode("discord", code, "user1"));
  // Code should be consumed
  EXPECT_FALSE(pm.VerifyCode("discord", code, "user2"));
}

TEST(PairingManagerTest, Unpair) {
  auto logger = make_null_logger();
  quantclaw::PairingManager pm(logger);

  auto code = pm.GenerateCode("discord");
  pm.VerifyCode("discord", code, "user1");
  EXPECT_TRUE(pm.IsPaired("discord", "user1"));

  pm.Unpair("discord", "user1");
  EXPECT_FALSE(pm.IsPaired("discord", "user1"));
}

TEST(PairingManagerTest, PairedSendersList) {
  auto logger = make_null_logger();
  quantclaw::PairingManager pm(logger);

  auto code1 = pm.GenerateCode("ch");
  pm.VerifyCode("ch", code1, "a");
  auto code2 = pm.GenerateCode("ch");
  pm.VerifyCode("ch", code2, "b");

  auto senders = pm.PairedSenders("ch");
  EXPECT_EQ(senders.size(), 2);
}

// --- SessionResolver ---

TEST(SessionResolverTest, MainScope) {
  auto key = quantclaw::SessionResolver::ResolveSessionKey(
      quantclaw::DmScope::kMain, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:main");
}

TEST(SessionResolverTest, PerPeerScope) {
  auto key = quantclaw::SessionResolver::ResolveSessionKey(
      quantclaw::DmScope::kPerPeer, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:peer:user1");
}

TEST(SessionResolverTest, PerChannelPeerScope) {
  auto key = quantclaw::SessionResolver::ResolveSessionKey(
      quantclaw::DmScope::kPerChannelPeer, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:discord:user1");
}

TEST(SessionResolverTest, PerAccountChannelPeerScope) {
  auto key = quantclaw::SessionResolver::ResolveSessionKey(
      quantclaw::DmScope::kPerAccountChannelPeer, "main", "discord", "user1",
      "acct1");
  EXPECT_EQ(key, "agent:main:acct1:discord:user1");
}

TEST(SessionResolverTest, PerAccountChannelPeerDefaultAccount) {
  auto key = quantclaw::SessionResolver::ResolveSessionKey(
      quantclaw::DmScope::kPerAccountChannelPeer, "main", "discord", "user1");
  EXPECT_EQ(key, "agent:main:default:discord:user1");
}

// --- Group Activation ---

TEST(GroupActivationTest, AlwaysActivates) {
  EXPECT_TRUE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kAlways, "hello", "Bot"));
}

TEST(GroupActivationTest, MentionDetected) {
  EXPECT_TRUE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kMention, "Hey @Bot how are you?", "Bot"));
}

TEST(GroupActivationTest, MentionCaseInsensitive) {
  EXPECT_TRUE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kMention, "Hello @bot!", "Bot"));
}

TEST(GroupActivationTest, NoMentionNoActivation) {
  EXPECT_FALSE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kMention, "Hello everyone!", "Bot"));
}

TEST(GroupActivationTest, CustomMentionPattern) {
  std::vector<std::string> patterns = {"<@\\d+>"};
  EXPECT_TRUE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kMention, "Hey <@12345> help", "", patterns));
}

TEST(GroupActivationTest, NoMatchWithEmptyBotName) {
  EXPECT_FALSE(quantclaw::SessionResolver::ShouldActivateGroup(
      quantclaw::GroupActivation::kMention, "Hello world", ""));
}
