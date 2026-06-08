// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_agent.hpp"

#include "quantclaw/config.hpp"
#include "quantclaw/core/message_commands.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw::gateway {

AgentHandler::AgentHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void AgentHandler::RegisterHandlers(GatewayServer& /*server*/) {
  // --- agent.request ---
  ctx_.server->RegisterHandler(
      methods::kAgentRequest,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleAgentRequest(params, client);
      });

  // --- agent.stop ---
  ctx_.server->RegisterHandler(
      methods::kAgentStop,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleAgentStop(params, client);
      });

  // --- chain.execute ---
  ctx_.server->RegisterHandler(
      methods::kChainExecute,
      [this](const nlohmann::json& params,
             ClientConnection& client) -> nlohmann::json {
        return HandleChainExecute(params, client);
      });
}

AgentHandler::AgentRequestResult AgentHandler::ExecuteAgentRequest(
    const nlohmann::json& params, ClientConnection& /*client*/,
    AgentEventCallback event_callback) {
  std::string session_key = params.value("sessionKey", "agent:main:main");
  std::string message = params.value("message", "");
  std::string error_message;

  if (message.empty()) {
    throw std::runtime_error("message is required");
  }

  // --- In-conversation slash command interception ---
  {
    quantclaw::MessageCommandParser::Handlers cmd_handlers;
    cmd_handlers.reset_session = [this](const std::string& key) {
      ctx_.session_manager->ResetSession(key);
    };
    cmd_handlers.compact_session = [this](const std::string& key) {
      auto history = ctx_.session_manager->GetHistory(key, -1);
      if (history.size() > 20) {
        ctx_.session_manager->ResetSession(key);
        int keep = std::min(20, static_cast<int>(history.size()));
        const auto start_index =
            history.size() - static_cast<size_t>(keep);
        for (size_t i = start_index; i < history.size(); ++i) {
          ctx_.session_manager->AppendMessage(key, history[i]);
        }
        if (ctx_.logger) {
          ctx_.logger->info("Compacted session {}: kept {} of {} messages", key,
                      keep, history.size());
        }
      }
    };
    cmd_handlers.get_status = [this](const std::string& key) {
      auto history = ctx_.session_manager->GetHistory(key, -1);
      return "Session: " + key +
             "\nMessages: " + std::to_string(history.size());
    };

    quantclaw::MessageCommandParser cmd_parser(std::move(cmd_handlers));
    auto cmd_result = cmd_parser.Parse(message, session_key);
    if (cmd_result.handled) {
      return {session_key, cmd_result.reply, ""};
    }
  }

  // Get or create session
  ctx_.session_manager->GetOrCreate(session_key, "", "cli");

  // Auto-generate display_name from first user message
  auto sessions = ctx_.session_manager->ListSessions();
  for (const auto& s : sessions) {
    if (s.session_key == session_key && s.display_name == session_key) {
      std::string truncated = message.substr(0, 50);
      ctx_.session_manager->UpdateDisplayName(session_key, truncated);
      break;
    }
  }

  // Append user message
  ctx_.session_manager->AppendMessage(session_key, "user", message);

  // Build system prompt
  std::string system_prompt = ctx_.prompt_builder->BuildFull();

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

  // Remove the last message
  if (!llm_history.empty()) {
    llm_history.pop_back();
  }

  // Send streaming events to the client
  std::string final_response;
  auto wrapped_callback = [&event_callback, &final_response, &error_message](
                              const quantclaw::AgentEvent& event) {
    event_callback(event);
    if (event.type != events::kMessageEnd) {
      return;
    }
    if (event.data.contains("error") && event.data["error"].is_string()) {
      error_message = event.data["error"].get<std::string>();
      return;
    }
    if (event.data.contains("content") && event.data["content"].is_string()) {
      final_response = event.data["content"].get<std::string>();
    }
  };

  auto new_messages = ctx_.agent_loop->ProcessMessageStream(
      message, llm_history, system_prompt, wrapped_callback);

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

  return {session_key, final_response, ""};
}

nlohmann::json AgentHandler::HandleAgentRequest(
    const nlohmann::json& params, ClientConnection& client) {
  auto result = ExecuteAgentRequest(
      params, client,
      [this, &client](const quantclaw::AgentEvent& event) {
        RpcEvent rpc_event;
        rpc_event.event = event.type;
        rpc_event.payload = event.data;
        ctx_.server->SendEventTo(client.connection_id, rpc_event);
      });
  return {{"sessionKey", result.session_key},
          {"response", result.final_response}};
}

nlohmann::json AgentHandler::HandleAgentStop(
    const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  ctx_.agent_loop->Stop();
  return {{"ok", true}};
}

nlohmann::json AgentHandler::HandleChainExecute(
    const nlohmann::json& params, ClientConnection& /*client*/) {
  auto chain_def = quantclaw::ToolChainExecutor::ParseChain(params);
  quantclaw::ToolExecutorFn executor =
      [this](const std::string& name, const nlohmann::json& args) {
        return ctx_.tool_registry->ExecuteTool(name, args);
      };
  quantclaw::ToolChainExecutor chain_executor(executor, ctx_.logger);
  auto result = chain_executor.Execute(chain_def);
  return quantclaw::ToolChainExecutor::ResultToJson(result);
}

}  // namespace quantclaw::gateway
