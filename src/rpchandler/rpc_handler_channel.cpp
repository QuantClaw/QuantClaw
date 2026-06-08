// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_channel.hpp"

#include "quantclaw/config.hpp"

#include <cctype>
#include <chrono>
#include <unordered_set>

namespace quantclaw::gateway {

ChannelHandler::ChannelHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void ChannelHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- channels.list ---
  ctx_.server->RegisterHandler(
      methods::kChannelsList,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleChannelsList(params, client);
      });

  // --- channels.status ---
  ctx_.server->RegisterHandler(
      methods::kChannelsStatus,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleChannelsStatus(params, client);
      });

  // --- channels.logout (OpenClaw compat stub) ---
  ctx_.server->RegisterHandler(
      "channels.logout",
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleChannelsLogout(params, client);
      });

  // --- agents.list (OpenClaw multi-agent compat stub) ---
  ctx_.server->RegisterHandler(
      "agents.list",
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleAgentsList(params, client);
      });
}

nlohmann::json ChannelHandler::HandleChannelsList(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  nlohmann::json result = nlohmann::json::array();
  // CLI channel is always present
  result.push_back({{"id", "cli"},
                    {"type", "cli"},
                    {"enabled", true},
                    {"status", "active"}});
  // Add configured channels
  const QuantClawConfig& config = *ctx_.config;
  for (const auto& [id, ch] : config.channels) {
    nlohmann::json entry;
    entry["id"] = id;
    entry["type"] = id;
    entry["enabled"] = ch.enabled;
    entry["status"] = ch.enabled ? "active" : "disabled";
    result.push_back(entry);
  }
  return result;
}

nlohmann::json ChannelHandler::HandleChannelsStatus(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  const auto running_adapters =
      ctx_.running_adapters_fn ? ctx_.running_adapters_fn() : std::vector<std::string>{};
  const std::unordered_set<std::string> running_set(running_adapters.begin(),
                                                    running_adapters.end());

  nlohmann::json channel_order = nlohmann::json::array();
  nlohmann::json channel_labels = nlohmann::json::object();
  nlohmann::json channels = nlohmann::json::object();
  nlohmann::json channel_accounts = nlohmann::json::object();
  nlohmann::json channel_default_account = nlohmann::json::object();

  auto add_channel = [&](const std::string& cid, bool enabled, bool configured,
                         bool running, const std::string& label) {
    channel_order.push_back(cid);
    channel_labels[cid] = label;
    channels[cid] = {{"enabled", enabled},
                     {"running", running},
                     {"configured", configured}};
    nlohmann::json account;
    account["accountId"] = cid + ":default";
    account["enabled"] = enabled;
    account["configured"] = configured;
    account["running"] = running;
    account["connected"] = running;
    channel_accounts[cid] = nlohmann::json::array({account});
    channel_default_account[cid] = cid + ":default";
  };

  add_channel("cli", true, true, true, "CLI");
  const QuantClawConfig& config = *ctx_.config;
  for (const auto& [cid, ch] : config.channels) {
    std::string label = cid;
    label[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    add_channel(cid, ch.enabled, true,
                ch.enabled && running_set.count(cid) > 0, label);
  }

  return {{"ts", now_ms},
          {"channelOrder", channel_order},
          {"channelLabels", channel_labels},
          {"channels", channels},
          {"channelAccounts", channel_accounts},
          {"channelDefaultAccountId", channel_default_account}};
}

nlohmann::json ChannelHandler::HandleChannelsLogout(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string id = params.value("id", "");
  if (ctx_.logger) {
    ctx_.logger->info("channels.logout requested for channel '{}'", id);
  }
  return {{"ok", true}};
}

nlohmann::json ChannelHandler::HandleAgentsList(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"defaultId", "main"},
          {"mainKey", "agent:main:main"},
          {"scope", "local"},
          {"agents",
           nlohmann::json::array(
               {nlohmann::json{{"id", "main"},
                               {"name", "QuantClaw Agent"},
                               {"identity",
                                {{"name", "QuantClaw Agent"},
                                 {"theme", "default"},
                                 {"emoji", "\xF0\x9F\xA6\x9E"},
                                 {"avatar", ""}}}}})}};
}

}  // namespace quantclaw::gateway
