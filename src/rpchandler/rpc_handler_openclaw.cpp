// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_openclaw.hpp"

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/constants.hpp"

namespace quantclaw::gateway {

OpenClawCompatHandler::OpenClawCompatHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void OpenClawCompatHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- chat.send (OpenClaw) ---
  ctx_.server->RegisterHandler(methods::kOcChatSend,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleChatSend(params, client);
      });

  // --- chat.history ---
  ctx_.server->RegisterHandler(methods::kOcChatHistory,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleChatHistory(params, client);
      });

  // --- chat.abort ---
  ctx_.server->RegisterHandler(methods::kOcChatAbort,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleChatAbort(params, client);
      });

  // --- health ---
  ctx_.server->RegisterHandler(methods::kOcHealth,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleHealth(params, client);
      });

  // --- status ---
  ctx_.server->RegisterHandler(methods::kOcStatus,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleStatus(params, client);
      });

  // --- models.list ---
  ctx_.server->RegisterHandler(methods::kOcModelsList,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleModelsList(params, client);
      });

  // --- tools.catalog ---
  ctx_.server->RegisterHandler(methods::kOcToolsCatalog,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleToolsCatalog(params, client);
      });
}

nlohmann::json OpenClawCompatHandler::HandleChatSend(const nlohmann::json& params, ClientConnection& client) {
  std::string session_key = params.value("sessionKey", "agent:main:main");
  std::string message = params.value("message", "");
  std::string run_id = params.value("idempotencyKey", "");
  
  if (message.empty()) {
    throw std::runtime_error("message is required");
  }

  // Get or create session
  ctx_.session_manager->GetOrCreate(session_key, "", "cli");
  
  // Append user message
  ctx_.session_manager->AppendMessage(session_key, "user", message);

  // Build system prompt
  std::string system_prompt = "You are a helpful assistant.";
  
  // Load history
  auto history = ctx_.session_manager->GetHistory(session_key, 50);

  // Convert SessionMessages to LLM Messages
  std::vector<quantclaw::Message> llm_history;
  for (const auto& smsg : history) {
    quantclaw::Message m;
    m.role = smsg.role;
    m.content = smsg.content;
    llm_history.push_back(m);
  }

  // Remove the last message (the one we just added)
  if (!llm_history.empty()) {
    llm_history.pop_back();
  }

  // Send streaming events to the client
  std::string final_response;
  std::string error_message;
  std::string cumulative_text;
  
  auto event_callback = [this, &client, &final_response, &error_message, 
                         &cumulative_text, &run_id, &session_key]
                        (const quantclaw::AgentEvent& event) {
    // Build chat event payload
    nlohmann::json payload;
    
    if (event.type == events::kTextDelta) {
      std::string text = event.data.value("text", "");
      cumulative_text += text;
      
      payload["state"] = "delta";
      payload["message"]["content"] = cumulative_text;
      if (!run_id.empty()) {
        payload["runId"] = run_id;
      }
      payload["sessionKey"] = session_key;
      
      ctx_.server->SendEventTo(client.connection_id, {"chat", payload});
    } else if (event.type == events::kMessageEnd) {
      if (event.data.contains("error") && event.data["error"].is_string()) {
        error_message = event.data["error"].get<std::string>();
        
        payload["state"] = "error";
        payload["errorMessage"] = error_message;
        if (!run_id.empty()) {
          payload["runId"] = run_id;
        }
        payload["sessionKey"] = session_key;
        
        ctx_.server->SendEventTo(client.connection_id, {"chat", payload});
      } else {
        if (event.data.contains("content") && event.data["content"].is_string()) {
          final_response = event.data["content"].get<std::string>();
        }
        
        payload["state"] = "final";
        payload["message"]["content"] = final_response;
        if (!run_id.empty()) {
          payload["runId"] = run_id;
        }
        payload["sessionKey"] = session_key;
        
        ctx_.server->SendEventTo(client.connection_id, {"chat", payload});
      }
    }
  };

  auto new_messages = ctx_.agent_loop->ProcessMessageStream(
      message, llm_history, system_prompt, event_callback);

  // Persist all new messages
  for (const auto& msg : new_messages) {
    quantclaw::SessionMessage smsg;
    smsg.role = msg.role;
    smsg.content = msg.content;
    ctx_.session_manager->AppendMessage(session_key, smsg);
  }

  if (!error_message.empty()) {
    throw std::runtime_error(error_message);
  }

  return {{"sessionKey", session_key}, {"response", final_response}};
}

nlohmann::json OpenClawCompatHandler::HandleChatHistory(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string session_key = params.value("sessionKey", "");
  int limit = params.value("limit", -1);

  if (session_key.empty()) {
    throw std::runtime_error("sessionKey is required");
  }

  auto history = ctx_.session_manager->GetHistory(session_key, limit);

  nlohmann::json messages = nlohmann::json::array();
  for (const auto& msg : history) {
    nlohmann::json entry;
    entry["role"] = msg.role;
    entry["timestamp"] = msg.timestamp;

    nlohmann::json content_arr = nlohmann::json::array();
    for (const auto& block : msg.content) {
      content_arr.push_back(block.ToJson());
    }
    entry["content"] = content_arr;

    if (msg.usage) {
      entry["usage"] = msg.usage->ToJson();
    }

    messages.push_back(entry);
  }
  return {{"messages", messages}, {"thinkingLevel", nullptr}};
}

nlohmann::json OpenClawCompatHandler::HandleChatAbort(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  ctx_.agent_loop->Stop();
  return {{"ok", true}};
}

nlohmann::json OpenClawCompatHandler::HandleHealth(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  return {{"status", "ok"},
          {"uptime", ctx_.server->GetUptimeSeconds()},
          {"version", quantclaw::kVersion}};
}

nlohmann::json OpenClawCompatHandler::HandleStatus(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  auto sessions = ctx_.session_manager->ListSessions();

  nlohmann::json recent = nlohmann::json::array();
  int recent_count = std::min(5, static_cast<int>(sessions.size()));
  const auto recent_start = sessions.size() - static_cast<size_t>(recent_count);
  for (size_t i = recent_start; i < sessions.size(); ++i) {
    recent.push_back({{"key", sessions[i].session_key},
                      {"sessionId", sessions[i].session_id},
                      {"updatedAt", sessions[i].updated_at},
                      {"model", ctx_.config->agent.model}});
  }

  return {{"running", true},
          {"port", ctx_.server->GetPort()},
          {"connections", ctx_.server->GetConnectionCount()},
          {"uptime", ctx_.server->GetUptimeSeconds()},
          {"version", quantclaw::kVersion},
          {"heartbeat",
           {{"defaultAgentId", "default"},
            {"agents", nlohmann::json::array()}}},
          {"channelSummary", nlohmann::json::array()},
          {"queuedSystemEvents", nlohmann::json::array()},
          {"sessions",
           {{"count", sessions.size()},
            {"paths", nlohmann::json::array()},
            {"defaults",
             {{"model", ctx_.config->agent.model},
              {"contextTokens", ctx_.config->agent.max_tokens}}},
            {"recent", recent},
            {"byAgent", nlohmann::json::array()}}}};
}

nlohmann::json OpenClawCompatHandler::HandleModelsList(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  nlohmann::json models = nlohmann::json::array();

  models.push_back({{"id", ctx_.config->agent.model},
                    {"provider", "default"},
                    {"active", true}});

  if (ctx_.provider_registry) {
    for (const auto& pid : ctx_.provider_registry->ProviderIds()) {
      auto p = ctx_.provider_registry->GetProvider(pid);
      if (p) {
        for (const auto& m : p->GetSupportedModels()) {
          if (m == ctx_.config->agent.model) continue;
          models.push_back({{"id", m}, {"provider", pid}, {"active", false}});
        }
      }
    }
  }

  return {{"models", models},
          {"current", ctx_.config->agent.model},
          {"aliases", nlohmann::json::object()}};
}

nlohmann::json OpenClawCompatHandler::HandleToolsCatalog(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string agent_id = params.value("agentId", "main");
  auto schemas = ctx_.tool_registry->GetToolSchemas();

  nlohmann::json core_tools = nlohmann::json::array();
  for (const auto& schema : schemas) {
    core_tools.push_back({{"id", schema.name},
                          {"label", schema.name},
                          {"description", schema.description},
                          {"source", "core"},
                          {"optional", true},
                          {"defaultProfiles", nlohmann::json::array({"full", "coding"})}});
  }

  nlohmann::json profiles = nlohmann::json::array(
      {nlohmann::json{{"id", "minimal"}, {"label", "Minimal"}},
       nlohmann::json{{"id", "coding"}, {"label", "Coding"}},
       nlohmann::json{{"id", "messaging"}, {"label", "Messaging"}},
       nlohmann::json{{"id", "full"}, {"label", "Full"}}});

  nlohmann::json groups = nlohmann::json::array(
      {nlohmann::json{{"id", "core"},
                      {"label", "Core Tools"},
                      {"source", "core"},
                      {"tools", core_tools}}});

  return {{"agentId", agent_id}, {"profiles", profiles}, {"groups", groups}};
}

}  // namespace quantclaw::gateway
