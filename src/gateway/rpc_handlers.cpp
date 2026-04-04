// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "quantclaw/config.hpp"
#include "quantclaw/constants.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/core/message_commands.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/core/session_compaction.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/gateway/command_queue.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/platform/process.hpp"
#include "quantclaw/plugins/plugin_system.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/security/exec_approval.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/tools/tool_registry.hpp"

namespace quantclaw::gateway {

void register_rpc_handlers(
    GatewayServer& server,
    std::shared_ptr<quantclaw::SessionManager> session_manager,
    std::shared_ptr<quantclaw::AgentLoop> agent_loop,
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
    const quantclaw::QuantClawConfig& config,
    std::shared_ptr<spdlog::logger> logger, std::function<void()> reload_fn,
    std::shared_ptr<quantclaw::ProviderRegistry> provider_registry,
    std::shared_ptr<quantclaw::SkillLoader> skill_loader,
    std::shared_ptr<quantclaw::CronScheduler> cron_scheduler,
    std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr,
    quantclaw::PluginSystem* plugin_system, CommandQueue* command_queue,
    std::string log_file_path,
    std::function<std::vector<std::string>()> running_adapters_fn) {
  // --- gateway.health ---
  server.RegisterHandler(
      methods::kGatewayHealth,
      [&server, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        return {{"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", quantclaw::kVersion}};
      });

  // --- gateway.status ---
  server.RegisterHandler(methods::kGatewayStatus,
                         [&server, session_manager, logger](
                             const nlohmann::json& /*params*/,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           auto sessions = session_manager->ListSessions();
                           return {{"running", true},
                                   {"port", server.GetPort()},
                                   {"connections", server.GetConnectionCount()},
                                   {"uptime", server.GetUptimeSeconds()},
                                   {"sessions", sessions.size()},
                                   {"version", quantclaw::kVersion}};
                         });

  // --- config.get ---
  server.RegisterHandler(
      methods::kConfigGet,
      [&config, logger](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
        std::string path_param = params.value("path", "");

        // Build the full config object that the UI config form expects
        nlohmann::json full_config = {
            {"agent",
             {{"model", config.agent.model},
              {"maxIterations", config.agent.max_iterations},
              {"temperature", config.agent.temperature},
              {"maxTokens", config.agent.max_tokens},
              {"contextWindow", config.agent.context_window},
              {"thinking", config.agent.thinking},
              {"autoCompact", config.agent.auto_compact}}},
            {"gateway",
             {{"port", config.gateway.port}, {"bind", config.gateway.bind}}}};

        if (!path_param.empty()) {
          // Dot-path lookup for legacy callers
          if (path_param == "gateway.port")
            return config.gateway.port;
          if (path_param == "gateway.bind")
            return config.gateway.bind;
          if (path_param == "agent.model")
            return config.agent.model;
          if (path_param == "agent.maxIterations")
            return config.agent.max_iterations;
          if (path_param == "agent.temperature")
            return config.agent.temperature;
          throw std::runtime_error("Unknown config path: " + path_param);
        }

        // Return ConfigSnapshot shape expected by the UI
        auto config_path = QuantClawConfig::DefaultConfigPath();
        bool exists = std::filesystem::exists(config_path);
        std::string raw_str = full_config.dump(2);

        return {{"path", config_path},   {"exists", exists},
                {"raw", raw_str},        {"hash", ""},
                {"parsed", full_config}, {"valid", true},
                {"config", full_config}, {"issues", nlohmann::json::array()}};
      });

  // --- config.set ---
  server.RegisterHandler(
      methods::kConfigSet,
      [logger, reload_fn](const nlohmann::json& params,
                          ClientConnection& /*client*/) -> nlohmann::json {
        std::string path = params.value("path", "");
        if (path.empty()) {
          throw std::runtime_error("path is required");
        }
        if (!params.contains("value")) {
          throw std::runtime_error("value is required");
        }

        auto config_file = QuantClawConfig::DefaultConfigPath();
        QuantClawConfig::SetValue(config_file, path, params["value"]);

        // Trigger hot-reload so the running server picks up the change
        if (reload_fn) {
          reload_fn();
        }

        return {{"ok", true}, {"path", path}};
      });

  // --- Shared agent request helper ---
  // Extracted so both agent.request and chat.send can reuse the core logic
  struct AgentRequestResult {
    std::string session_key;
    std::string final_response;
    std::string error_message;
  };

  auto execute_agent_request =
      [session_manager, agent_loop, prompt_builder, logger](
          const nlohmann::json& params, ClientConnection& /*client*/,
          quantclaw::AgentEventCallback event_callback) -> AgentRequestResult {
    std::string session_key = params.value("sessionKey", "agent:main:main");
    std::string message = params.value("message", "");
    std::string error_message;

    if (message.empty()) {
      throw std::runtime_error("message is required");
    }

    // --- In-conversation slash command interception ---
    // Check if the message is a slash command (/new, /reset, /compact, etc.)
    // before forwarding to the LLM.
    {
      quantclaw::MessageCommandParser::Handlers cmd_handlers;
      cmd_handlers.reset_session = [session_manager](const std::string& key) {
        session_manager->ResetSession(key);
      };
      cmd_handlers.compact_session = [session_manager,
                                      logger](const std::string& key) {
        auto history = session_manager->GetHistory(key, -1);
        if (history.size() > 20) {
          // Simple truncation (keep last 20 messages)
          session_manager->ResetSession(key);
          int keep = std::min(20, static_cast<int>(history.size()));
          const auto start_index = history.size() - static_cast<size_t>(keep);
          for (size_t i = start_index; i < history.size(); ++i) {
            session_manager->AppendMessage(key, history[i]);
          }
          logger->info("Compacted session {}: kept {} of {} messages", key,
                       keep, history.size());
        }
      };
      cmd_handlers.get_status = [session_manager](const std::string& key) {
        auto history = session_manager->GetHistory(key, -1);
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
    session_manager->GetOrCreate(session_key, "", "cli");

    // Auto-generate display_name from first user message
    auto sessions = session_manager->ListSessions();
    for (const auto& s : sessions) {
      if (s.session_key == session_key && s.display_name == session_key) {
        std::string truncated = message.substr(0, 50);
        session_manager->UpdateDisplayName(session_key, truncated);
        break;
      }
    }

    // Append user message
    session_manager->AppendMessage(session_key, "user", message);

    // Build system prompt
    std::string system_prompt = prompt_builder->BuildFull();

    // Load history
    auto history = session_manager->GetHistory(session_key, 50);

    // Convert SessionMessages to LLM Messages (lossless copy)
    std::vector<quantclaw::Message> llm_history;
    for (const auto& smsg : history) {
      quantclaw::Message m;
      m.role = smsg.role;
      m.content = smsg.content;
      llm_history.push_back(m);
    }

    // Remove the last message (the one we just appended) since process_message
    // adds it
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

    auto new_messages = agent_loop->ProcessMessageStream(
        message, llm_history, system_prompt, wrapped_callback);

    // Persist all new messages (assistant + tool_result) to session transcript
    for (const auto& msg : new_messages) {
      quantclaw::SessionMessage smsg;
      smsg.role = msg.role;
      smsg.content = msg.content;
      session_manager->AppendMessage(session_key, smsg);
    }

    if (!error_message.empty()) {
      throw std::runtime_error(error_message);
    }

    return {session_key, final_response, ""};
  };

  // --- agent.request ---
  server.RegisterHandler(
      methods::kAgentRequest,
      [execute_agent_request, &server,
       logger](const nlohmann::json& params,
               ClientConnection& client) -> nlohmann::json {
        auto result = execute_agent_request(
            params, client,
            [&server, &client, logger](const quantclaw::AgentEvent& event) {
              RpcEvent rpc_event;
              rpc_event.event = event.type;
              rpc_event.payload = event.data;
              server.SendEventTo(client.connection_id, rpc_event);
            });
        return {{"sessionKey", result.session_key},
                {"response", result.final_response}};
      });

  // --- agent.stop ---
  server.RegisterHandler(
      methods::kAgentStop,
      [agent_loop, logger](const nlohmann::json& /*params*/,
                           ClientConnection& /*client*/) -> nlohmann::json {
        agent_loop->Stop();
        return {{"ok", true}};
      });

  // --- sessions.list ---
  server.RegisterHandler(
      methods::kSessionsList,
      [session_manager, &config,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        int limit = params.value("limit", 0);
        int offset = params.value("offset", 0);

        auto sessions = session_manager->ListSessions();
        int total = static_cast<int>(sessions.size());
        int start = std::clamp(offset, 0, total);
        int end = (limit > 0) ? std::min(start + limit, total) : total;

        // Helper: Convert ISO timestamp "YYYY-MM-DDTHH:MM:SSZ" to milliseconds
        // since epoch
        auto iso_to_ms = [](const std::string& iso_str) -> int64_t {
          if (iso_str.empty())
            return 0;
          std::tm tm = {};
          std::istringstream ss(iso_str);
          ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
          if (ss.fail())
            return 0;
          tm.tm_isdst = 0;  // UTC has no DST
#ifdef _WIN32
          auto time_t_val = _mkgmtime64(&tm);
#else
          auto time_t_val = timegm(&tm);
#endif
          if (time_t_val < 0)
            return 0;
          return static_cast<int64_t>(time_t_val) * 1000;
        };

        nlohmann::json session_rows = nlohmann::json::array();
        for (size_t i = static_cast<size_t>(start);
             i < static_cast<size_t>(end); ++i) {
          const auto& s = sessions[i];
          nlohmann::json row;
          row["key"] = s.session_key;
          row["sessionId"] = s.session_id;
          row["displayName"] = s.display_name;
          row["surface"] = s.channel.empty() ? "cli" : s.channel;
          // Convert ISO timestamp to epoch ms
          row["updatedAt"] = iso_to_ms(s.updated_at);
          // Derive kind from key pattern
          if (s.session_key.find("group:") != std::string::npos) {
            row["kind"] = "group";
          } else if (s.session_key.find("global") != std::string::npos) {
            row["kind"] = "global";
          } else {
            row["kind"] = "direct";
          }
          // Parent/subagent metadata (only if set)
          if (!s.spawned_by.empty())
            row["spawnedBy"] = s.spawned_by;
          if (s.spawn_depth > 0)
            row["spawnDepth"] = s.spawn_depth;
          if (!s.subagent_role.empty())
            row["subagentRole"] = s.subagent_role;
          session_rows.push_back(row);
        }

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        return {{"ts", now_ms},
                {"path", ""},
                {"count", total},
                {"defaults",
                 {{"model", config.agent.model},
                  {"contextTokens", config.agent.context_window}}},
                {"sessions", session_rows}};
      });

  // --- sessions.history ---
  server.RegisterHandler(
      methods::kSessionsHistory,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        int limit = params.value("limit", -1);

        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, limit);

        nlohmann::json result = nlohmann::json::array();
        for (const auto& msg : history) {
          nlohmann::json entry;
          entry["role"] = msg.role;
          entry["timestamp"] = msg.timestamp;

          // Return full ContentBlock array
          nlohmann::json content_arr = nlohmann::json::array();
          for (const auto& block : msg.content) {
            content_arr.push_back(block.ToJson());
          }
          entry["content"] = content_arr;

          if (msg.usage) {
            entry["usage"] = msg.usage->ToJson();
          }

          result.push_back(entry);
        }
        return result;
      });

  // --- sessions.delete ---
  server.RegisterHandler(methods::kSessionsDelete,
                         [session_manager, logger](
                             const nlohmann::json& params,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           // UI sends "key"; legacy clients send "sessionKey"
                           std::string session_key = params.value("key", "");
                           if (session_key.empty())
                             session_key = params.value("sessionKey", "");
                           if (session_key.empty()) {
                             throw std::runtime_error("key is required");
                           }
                           bool deleted =
                               session_manager->DeleteSession(session_key);
                           return {{"ok", true}, {"deleted", deleted}};
                         });

  // --- sessions.reset ---
  server.RegisterHandler(methods::kSessionsReset,
                         [session_manager, logger](
                             const nlohmann::json& params,
                             ClientConnection& /*client*/) -> nlohmann::json {
                           std::string session_key =
                               params.value("sessionKey", "");
                           if (session_key.empty()) {
                             throw std::runtime_error("sessionKey is required");
                           }
                           session_manager->ResetSession(session_key);
                           return {{"ok", true}};
                         });

  // --- channels.list ---
  server.RegisterHandler(
      methods::kChannelsList,
      [&config, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json result = nlohmann::json::array();
        // CLI channel is always present
        result.push_back({{"id", "cli"},
                          {"type", "cli"},
                          {"enabled", true},
                          {"status", "active"}});
        // Add configured channels
        for (const auto& [id, ch] : config.channels) {
          nlohmann::json entry;
          entry["id"] = id;
          entry["type"] =
              id;  // type is typically same as id (discord, telegram, etc.)
          entry["enabled"] = ch.enabled;
          entry["status"] = ch.enabled ? "active" : "disabled";
          result.push_back(entry);
        }
        return result;
      });

  // --- channels.status ---
  // Returns ChannelsStatusSnapshot shape expected by the UI.
  server.RegisterHandler(
      methods::kChannelsStatus,
      [&config, logger,
       running_adapters_fn](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        const auto running_adapters = running_adapters_fn
                                          ? running_adapters_fn()
                                          : std::vector<std::string>{};
        const std::unordered_set<std::string> running_set(
            running_adapters.begin(), running_adapters.end());

        // Build ordered list: cli first, then configured channels
        nlohmann::json channel_order = nlohmann::json::array();
        nlohmann::json channel_labels = nlohmann::json::object();
        nlohmann::json channels = nlohmann::json::object();
        nlohmann::json channel_accounts = nlohmann::json::object();
        nlohmann::json channel_default_account = nlohmann::json::object();

        auto add_channel = [&](const std::string& cid, bool enabled,
                               bool configured, bool running,
                               const std::string& label) {
          channel_order.push_back(cid);
          channel_labels[cid] = label;
          channels[cid] = {{"enabled", enabled},
                           {"running", running},
                           {"configured", configured}};
          // Each channel has a default account entry
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
        for (const auto& [cid, ch] : config.channels) {
          std::string label = cid;
          label[0] = static_cast<char>(
              std::toupper(static_cast<unsigned char>(label[0])));
          add_channel(cid, ch.enabled, true,
                      ch.enabled && running_set.count(cid) > 0, label);
        }

        return {{"ts", now_ms},
                {"channelOrder", channel_order},
                {"channelLabels", channel_labels},
                {"channels", channels},
                {"channelAccounts", channel_accounts},
                {"channelDefaultAccountId", channel_default_account}};
      });

  // --- channels.logout (OpenClaw compat stub) ---
  server.RegisterHandler(
      "channels.logout",
      [logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string id = params.value("id", "");
        logger->info("channels.logout requested for channel '{}'", id);
        return {{"ok", true}};
      });

  // --- agents.list (OpenClaw multi-agent compat stub) ---
  // QuantClaw uses a single "main" agent; return AgentsListResult shape.
  server.RegisterHandler(
      "agents.list",
      [](const nlohmann::json& /*params*/,
         ClientConnection& /*client*/) -> nlohmann::json {
        return {{"defaultId", "main"},
                {"mainKey", "agent:main:main"},
                {"scope", "local"},
                {"agents", nlohmann::json::array(
                               {nlohmann::json{{"id", "main"},
                                               {"name", "QuantClaw Agent"},
                                               {"identity",
                                                {{"name", "QuantClaw Agent"},
                                                 {"theme", "default"},
                                                 {"emoji", "\xF0\x9F\xA6\x9E"},
                                                 {"avatar", ""}}}}})}};
      });

  // --- chain.execute ---
  server.RegisterHandler(
      methods::kChainExecute,
      [tool_registry, logger](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        auto chain_def = quantclaw::ToolChainExecutor::ParseChain(params);
        quantclaw::ToolExecutorFn executor =
            [tool_registry](const std::string& name,
                            const nlohmann::json& args) {
              return tool_registry->ExecuteTool(name, args);
            };
        quantclaw::ToolChainExecutor chain_executor(executor, logger);
        auto result = chain_executor.Execute(chain_def);
        return quantclaw::ToolChainExecutor::ResultToJson(result);
      });

  // --- config.reload / config.apply (OpenClaw alias) ---
  if (reload_fn) {
    auto reload_handler =
        [reload_fn, logger](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
      reload_fn();
      return {{"ok", true}};
    };
    server.RegisterHandler(methods::kConfigReload, reload_handler);
    // OpenClaw clients use "config.apply" for hot-reload
    server.RegisterHandler("config.apply", reload_handler);
  }

  // ================================================================
  // OpenClaw-compatible RPC handlers (protocol shim)
  // ================================================================

  // --- chat.send (OpenClaw) ---
  // Translates QuantClaw agent events to OpenClaw format
  server.RegisterHandler(
      methods::kOcChatSend,
      [execute_agent_request, &server,
       logger](const nlohmann::json& params,
               ClientConnection& client) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "agent:main:main");
        std::string idempotency_key = params.value("idempotencyKey", "");
        std::string streamed_text;
        std::string accumulated_text;
        auto result = execute_agent_request(
            params, client,
            [&server, &client, logger, session_key, idempotency_key,
             &streamed_text,
             &accumulated_text](const quantclaw::AgentEvent& event) {
              RpcEvent rpc_event;

              if (event.type == events::kTextDelta) {
                // agent.text_delta → event "chat" {state:"delta",
                // message:{content}, runId, sessionKey}
                const std::string chunk = event.data.value("text", "");
                streamed_text += chunk;
                accumulated_text += chunk;
                rpc_event.event = events::kOcChat;
                rpc_event.payload = {
                    {"state", "delta"},
                    {"message",
                     {{"role", "assistant"}, {"content", streamed_text}}},
                    {"content", streamed_text},
                    {"runId", idempotency_key},
                    {"sessionKey", session_key}};
              } else if (event.type == events::kToolUse) {
                // agent.tool_use → event "agent" {stream:"tool",
                // data:{id,name,input}}
                streamed_text.clear();
                rpc_event.event = events::kOcAgent;
                rpc_event.payload = {
                    {"stream", "tool"},
                    {"data",
                     {{"id", event.data.value("id", "")},
                      {"name", event.data.value("name", "")},
                      {"input",
                       event.data.value("input", nlohmann::json::object())}}}};
              } else if (event.type == events::kToolResult) {
                // agent.tool_result → event "agent" {stream:"tool_result",
                // data:{tool_use_id,content}}
                rpc_event.event = events::kOcAgent;
                rpc_event.payload = {
                    {"stream", "tool_result"},
                    {"data",
                     {{"tool_use_id", event.data.value("tool_use_id", "")},
                      {"content", event.data.value("content", "")}}}};
              } else if (event.type == events::kMessageEnd) {
                // agent.message_end → event "chat" {state:"final", message,
                // runId, sessionKey}
                rpc_event.event = events::kOcChat;
                if (event.data.contains("error") &&
                    event.data["error"].is_string()) {
                  rpc_event.payload = {
                      {"state", "error"},
                      {"errorMessage", event.data["error"].get<std::string>()},
                      {"runId", idempotency_key},
                      {"sessionKey", session_key}};
                } else {
                  std::string final_text =
                      event.data.value("content", accumulated_text);
                  rpc_event.payload = {
                      {"state", "final"},
                      {"message",
                       {{"role", "assistant"}, {"content", final_text}}},
                      {"content", final_text},
                      {"runId", idempotency_key},
                      {"sessionKey", session_key}};
                }
              } else {
                // Pass through any other events as-is
                rpc_event.event = event.type;
                rpc_event.payload = event.data;
              }

              server.SendEventTo(client.connection_id, rpc_event);
            });
        return {{"sessionKey", result.session_key},
                {"response", result.final_response}};
      });

  // --- chat.history (alias for sessions.history) ---
  server.RegisterHandler(
      methods::kOcChatHistory,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        int limit = params.value("limit", -1);

        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, limit);

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
        // UI expects {messages:[], thinkingLevel?:string}
        return {{"messages", messages}, {"thinkingLevel", nullptr}};
      });

  // --- chat.abort (alias for agent.stop) ---
  server.RegisterHandler(
      methods::kOcChatAbort,
      [agent_loop, logger](const nlohmann::json& /*params*/,
                           ClientConnection& /*client*/) -> nlohmann::json {
        agent_loop->Stop();
        return {{"ok", true}};
      });

  // --- health (alias for gateway.health) ---
  server.RegisterHandler(
      methods::kOcHealth,
      [&server, logger](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
        return {{"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", quantclaw::kVersion}};
      });

  // --- status (alias for gateway.status) ---
  // Returns an OpenClaw-compatible StatusSummary so OC clients don't crash
  // on missing fields (heartbeat, sessions.byAgent, channelSummary, etc.).
  server.RegisterHandler(
      methods::kOcStatus,
      [&server, session_manager, &config,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        auto sessions = session_manager->ListSessions();

        // Build sessions.recent (last 5, lightweight)
        nlohmann::json recent = nlohmann::json::array();
        int recent_count = std::min(5, static_cast<int>(sessions.size()));
        const auto recent_start =
            sessions.size() - static_cast<size_t>(recent_count);
        for (size_t i = recent_start; i < sessions.size(); ++i) {
          recent.push_back({{"key", sessions[i].session_key},
                            {"sessionId", sessions[i].session_id},
                            {"updatedAt", sessions[i].updated_at},
                            {"model", config.agent.model}});
        }

        return {// QuantClaw fields
                {"running", true},
                {"port", server.GetPort()},
                {"connections", server.GetConnectionCount()},
                {"uptime", server.GetUptimeSeconds()},
                {"version", quantclaw::kVersion},
                // OpenClaw compatibility fields
                {"heartbeat",
                 {{"defaultAgentId", "default"},
                  {"agents", nlohmann::json::array()}}},
                {"channelSummary", nlohmann::json::array()},
                {"queuedSystemEvents", nlohmann::json::array()},
                {"sessions",
                 {{"count", sessions.size()},
                  {"paths", nlohmann::json::array()},
                  {"defaults",
                   {{"model", config.agent.model},
                    {"contextTokens", config.agent.max_tokens}}},
                  {"recent", recent},
                  {"byAgent", nlohmann::json::array()}}}};
      });

  // --- models.list ---
  // Returns {models:[], current, aliases} shape expected by the UI.
  server.RegisterHandler(
      methods::kOcModelsList,
      [&config, provider_registry,
       logger](const nlohmann::json& /*params*/,
               ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json models = nlohmann::json::array();

        // Active model first
        models.push_back({{"id", config.agent.model},
                          {"provider", "default"},
                          {"active", true}});

        // List models from registered providers
        if (provider_registry) {
          for (const auto& pid : provider_registry->ProviderIds()) {
            auto p = provider_registry->GetProvider(pid);
            if (p) {
              for (const auto& m : p->GetSupportedModels()) {
                if (m == config.agent.model)
                  continue;  // already added
                models.push_back(
                    {{"id", m}, {"provider", pid}, {"active", false}});
              }
            }
          }
        }

        return {{"models", models},
                {"current", config.agent.model},
                {"aliases", nlohmann::json::object()}};
      });

  // --- tools.catalog ---
  // Returns ToolsCatalogResult shape expected by the UI.
  server.RegisterHandler(
      methods::kOcToolsCatalog,
      [tool_registry, logger](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        std::string agent_id = params.value("agentId", "main");
        auto schemas = tool_registry->GetToolSchemas();

        // Build tools list for the "core" group
        nlohmann::json core_tools = nlohmann::json::array();
        for (const auto& schema : schemas) {
          core_tools.push_back(
              {{"id", schema.name},
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

        nlohmann::json groups =
            nlohmann::json::array({nlohmann::json{{"id", "core"},
                                                  {"label", "Core Tools"},
                                                  {"source", "core"},
                                                  {"tools", core_tools}}});

        return {
            {"agentId", agent_id}, {"profiles", profiles}, {"groups", groups}};
      });

  // --- sessions.preview ---
  server.RegisterHandler(
      methods::kOcSessionsPreview,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key, 1);
        if (history.empty()) {
          return nlohmann::json::object();
        }

        const auto& msg = history.back();
        nlohmann::json entry;
        entry["role"] = msg.role;
        entry["timestamp"] = msg.timestamp;

        nlohmann::json content_arr = nlohmann::json::array();
        for (const auto& block : msg.content) {
          content_arr.push_back(block.ToJson());
        }
        entry["content"] = content_arr;

        return entry;
      });

  // --- sessions.patch ---
  server.RegisterHandler(
      methods::kSessionsPatch,
      [session_manager,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        // UI sends "key"; legacy clients send "sessionKey"
        std::string session_key = params.value("key", "");
        if (session_key.empty())
          session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("key is required");
        }

        // Apply displayName / label rename
        if (params.contains("displayName")) {
          session_manager->UpdateDisplayName(
              session_key, params["displayName"].get<std::string>());
        } else if (params.contains("label")) {
          if (!params["label"].is_null()) {
            session_manager->UpdateDisplayName(
                session_key, params["label"].get<std::string>());
          }
        }
        // thinkingLevel, verboseLevel, reasoningLevel are stored in session
        // metadata in a full implementation; here we acknowledge them without
        // persisting.

        return {{"ok", true},
                {"path", ""},
                {"key", session_key},
                {"entry", {{"sessionId", ""}}}};
      });

  // --- sessions.compact ---
  server.RegisterHandler(
      methods::kSessionsCompact,
      [session_manager, agent_loop,
       logger](const nlohmann::json& params,
               ClientConnection& /*client*/) -> nlohmann::json {
        std::string session_key = params.value("sessionKey", "");
        if (session_key.empty()) {
          throw std::runtime_error("sessionKey is required");
        }

        auto history = session_manager->GetHistory(session_key);

        // Convert SessionMessage list to JSON for compaction API
        std::vector<nlohmann::json> history_json;
        for (const auto& m : history) {
          history_json.push_back(m.ToJsonl());
        }

        quantclaw::SessionCompaction compaction(logger);
        quantclaw::SessionCompaction::Options opts;
        opts.max_messages = params.value("maxMessages", 100);
        opts.keep_recent = params.value("keepRecent", 20);

        if (!compaction.NeedsCompaction(history_json, opts)) {
          return {{"compacted", false}, {"reason", "below threshold"}};
        }

        auto compacted = compaction.Truncate(history_json, opts);

        return {{"compacted", true},
                {"originalCount", static_cast<int>(history.size())},
                {"newCount", static_cast<int>(compacted.size())}};
      });

  // --- skills.status ---
  if (skill_loader) {
    server.RegisterHandler(
        methods::kSkillsStatus,
        [skill_loader, &config,
         logger](const nlohmann::json& /*params*/,
                 ClientConnection& /*client*/) -> nlohmann::json {
          auto workspace_path =
              std::filesystem::path(quantclaw::platform::home_directory()) /
              ".quantclaw/agents/main/workspace";
          std::string managed_dir =
              (std::filesystem::path(quantclaw::platform::home_directory()) /
               ".quantclaw" / "skills")
                  .string();

          auto skills = skill_loader->LoadSkills(config.skills, workspace_path);

          nlohmann::json skill_entries = nlohmann::json::array();
          for (const auto& skill : skills) {
            bool gated = !skill_loader->CheckSkillGating(skill);
            std::string skill_key =
                skill.skill_key.empty() ? skill.name : skill.skill_key;

            // Build missing bins/env/config lists
            nlohmann::json missing_bins = nlohmann::json::array();
            nlohmann::json missing_env = nlohmann::json::array();

            // Build install options from SkillInstallInfo
            nlohmann::json install_opts = nlohmann::json::array();
            for (const auto& inst : skill.installs) {
              std::string kind = inst.EffectiveMethod();
              if (kind == "node" || kind == "go" || kind == "uv" ||
                  kind == "brew") {
                nlohmann::json opt;
                opt["id"] = kind + ":" + inst.EffectiveFormula();
                opt["kind"] = kind;
                opt["label"] =
                    inst.label.empty() ? inst.EffectiveFormula() : inst.label;
                nlohmann::json bins = nlohmann::json::array();
                for (const auto& b : inst.bins)
                  bins.push_back(b);
                if (bins.empty() && !inst.EffectiveBinary().empty()) {
                  bins.push_back(inst.EffectiveBinary());
                }
                opt["bins"] = bins;
                install_opts.push_back(opt);
              }
            }

            skill_entries.push_back(
                {{"name", skill.name},
                 {"description", skill.description},
                 {"source", "bundled"},
                 {"filePath", skill.root_dir.string()},
                 {"baseDir", skill.root_dir.string()},
                 {"skillKey", skill_key},
                 {"bundled", true},
                 {"primaryEnv", skill.primary_env},
                 {"emoji", skill.emoji},
                 {"homepage", skill.homepage},
                 {"always", skill.always},
                 {"disabled", false},
                 {"blockedByAllowlist", false},
                 {"eligible", !gated},
                 {"requirements",
                  {{"bins",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& b : skill.required_bins)
                        a.push_back(b);
                      return a;
                    }()},
                   {"env",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& e : skill.required_envs)
                        a.push_back(e);
                      return a;
                    }()},
                   {"config", nlohmann::json::array()},
                   {"os",
                    [&]() {
                      nlohmann::json a = nlohmann::json::array();
                      for (const auto& o : skill.os_restrict)
                        a.push_back(o);
                      return a;
                    }()}}},
                 {"missing",
                  {{"bins", missing_bins},
                   {"env", missing_env},
                   {"config", nlohmann::json::array()},
                   {"os", nlohmann::json::array()}}},
                 {"configChecks", nlohmann::json::array()},
                 {"install", install_opts}});
          }

          return {{"workspaceDir", workspace_path.string()},
                  {"managedSkillsDir", managed_dir},
                  {"skills", skill_entries}};
        });

    // --- skills.install ---
    server.RegisterHandler(
        methods::kSkillsInstall,
        [skill_loader, logger](const nlohmann::json& params,
                               ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("name", "");
          if (name.empty()) {
            throw std::runtime_error("skill name is required");
          }

          quantclaw::SkillMetadata meta;
          meta.name = name;
          meta.root_dir = params.value("rootDir", "");

          bool ok = skill_loader->InstallSkill(meta);
          return {{"ok", ok}, {"name", name}};
        });
  }

  // --- cron.list ---
  if (cron_scheduler) {
    server.RegisterHandler(
        methods::kCronList,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          int limit = params.value("limit", 0);
          int offset = params.value("offset", 0);
          auto jobs = cron_scheduler->ListJobs();
          int total = static_cast<int>(jobs.size());
          int start = std::clamp(offset, 0, total);
          int end = (limit > 0) ? std::min(start + limit, total) : total;

          auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

          auto tp_to_ms =
              [](std::chrono::system_clock::time_point tp) -> long long {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       tp.time_since_epoch())
                .count();
          };

          nlohmann::json job_list = nlohmann::json::array();
          for (size_t i = static_cast<size_t>(start);
               i < static_cast<size_t>(end); ++i) {
            const auto& job = jobs[i];
            long long last_ms = tp_to_ms(job.last_run);
            long long next_ms = tp_to_ms(job.next_run);

            nlohmann::json state = nlohmann::json::object();
            if (next_ms > 0)
              state["nextRunAtMs"] = next_ms;
            if (last_ms > 0)
              state["lastRunAtMs"] = last_ms;

            job_list.push_back(
                {{"id", job.id},
                 {"name", job.name},
                 {"description", ""},
                 {"enabled", job.enabled},
                 {"deleteAfterRun", false},
                 {"createdAtMs", now_ms},
                 {"updatedAtMs", now_ms},
                 {"schedule", {{"kind", "cron"}, {"expr", job.schedule}}},
                 {"sessionTarget", "main"},
                 {"wakeMode", "now"},
                 {"payload", {{"kind", "agentTurn"}, {"message", job.message}}},
                 {"state", state}});
          }

          bool has_more = end < total;
          return {{"jobs", job_list},
                  {"total", total},
                  {"offset", offset},
                  {"limit", limit > 0 ? limit : total},
                  {"hasMore", has_more},
                  {"nextOffset",
                   has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
        });

    // --- cron.add ---
    // Supports both flat format (schedule string + message) and the rich UI
    // format {name, schedule:{kind,expr,everyMs,...},
    // payload:{kind,message,...}, ...}
    server.RegisterHandler(
        methods::kCronAdd,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("name", "");
          std::string session_key =
              params.value("sessionKey", "agent:main:main");

          // Extract schedule expression (flat or nested)
          std::string schedule;
          if (params.contains("schedule") && params["schedule"].is_object()) {
            const auto& sched = params["schedule"];
            std::string kind = sched.value("kind", "cron");
            if (kind == "cron") {
              schedule = sched.value("expr", "");
            } else if (kind == "every") {
              // Convert everyMs to approximate cron expression
              long long every_ms = sched.value("everyMs", 3600000LL);
              long long every_min = std::max(1LL, every_ms / 60000LL);
              if (every_min < 60) {
                schedule = "*/" + std::to_string(every_min) + " * * * *";
              } else {
                long long every_hr = every_min / 60;
                schedule = "0 */" + std::to_string(every_hr) + " * * *";
              }
            } else if (kind == "at") {
              schedule = sched.value("at", "0 * * * *");
            }
          } else {
            schedule = params.value("schedule", "");
          }

          // Extract message (flat or nested in payload)
          std::string message;
          if (params.contains("payload") && params["payload"].is_object()) {
            message = params["payload"].value("message", "");
          } else {
            message = params.value("message", "");
          }

          if (name.empty() && !message.empty()) {
            name = message.substr(0, std::min(message.size(), (size_t)40));
          }
          if (schedule.empty() || message.empty()) {
            throw std::runtime_error("schedule and message are required");
          }

          auto id =
              cron_scheduler->AddJob(name, schedule, message, session_key);
          return {{"ok", true}, {"id", id}};
        });

    // --- cron.remove ---
    server.RegisterHandler(
        methods::kCronRemove,
        [cron_scheduler](const nlohmann::json& params,
                         ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }
          bool removed = cron_scheduler->RemoveJob(id);
          return {{"ok", removed}};
        });
  }

  // --- cron.update ---
  // Accepts both flat format and UI's {id, patch:{...}} format.
  if (cron_scheduler) {
    server.RegisterHandler(
        methods::kCronUpdate,
        [cron_scheduler,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }

          // Flatten nested patch object if present
          nlohmann::json flat = params;
          if (params.contains("patch") && params["patch"].is_object()) {
            for (auto& [k, v] : params["patch"].items()) {
              flat[k] = v;
            }
          }

          auto jobs = cron_scheduler->ListJobs();
          for (const auto& job : jobs) {
            if (job.id == id) {
              std::string name = flat.value("name", job.name);
              std::string schedule = job.schedule;
              std::string message = job.message;

              // Extract schedule from nested or flat
              if (flat.contains("schedule") && flat["schedule"].is_object()) {
                const auto& s = flat["schedule"];
                if (s.value("kind", "") == "cron") {
                  schedule = s.value("expr", job.schedule);
                }
              } else if (flat.contains("schedule") &&
                         flat["schedule"].is_string()) {
                schedule = flat["schedule"].get<std::string>();
              }

              // Extract message from nested payload or flat
              if (flat.contains("payload") && flat["payload"].is_object()) {
                message = flat["payload"].value("message", job.message);
              } else if (flat.contains("message")) {
                message = flat.value("message", job.message);
              }

              cron_scheduler->RemoveJob(job.id);
              auto new_id = cron_scheduler->AddJob(name, schedule, message,
                                                   job.session_key);
              return {{"ok", true}, {"id", new_id}};
            }
          }

          throw std::runtime_error("cron job not found: " + id);
        });

    // --- cron.run ---
    server.RegisterHandler(
        methods::kCronRun,
        [cron_scheduler, agent_loop, session_manager, prompt_builder,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string id = params.value("id", "");
          if (id.empty()) {
            throw std::runtime_error("cron job id is required");
          }

          auto jobs = cron_scheduler->ListJobs();
          for (const auto& job : jobs) {
            if (job.id == id || job.id.substr(0, id.size()) == id) {
              // Execute the cron job's message as an agent request
              auto session = session_manager->GetOrCreate(job.session_key,
                                                          job.name, "cron");
              auto history_msgs = session_manager->GetHistory(job.session_key);

              std::vector<quantclaw::Message> history;
              for (const auto& m : history_msgs) {
                quantclaw::Message msg;
                msg.role = m.role;
                msg.content = m.content;
                history.push_back(msg);
              }

              auto system_prompt = prompt_builder->BuildFull(job.session_key);
              auto new_msgs = agent_loop->ProcessMessage(job.message, history,
                                                         system_prompt);

              // Store messages
              for (const auto& msg : new_msgs) {
                quantclaw::SessionMessage sm;
                sm.role = msg.role;
                sm.content = msg.content;
                session_manager->AppendMessage(job.session_key, sm);
              }

              nlohmann::json r;
              r["ok"] = true;
              r["jobId"] = job.id;
              r["messagesGenerated"] = static_cast<int>(new_msgs.size());
              return r;
            }
          }

          throw std::runtime_error("cron job not found: " + id);
        });

    // --- cron.runs ---
    // Returns CronRunsResult shape expected by the UI.
    server.RegisterHandler(
        methods::kCronRuns,
        [cron_scheduler,
         logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string filter_id = params.value("id", "");
          int limit = params.value("limit", 0);
          int offset = params.value("offset", 0);
          auto jobs = cron_scheduler->ListJobs();

          auto tp_to_ms =
              [](std::chrono::system_clock::time_point tp) -> long long {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       tp.time_since_epoch())
                .count();
          };

          // Build synthetic run log entries from last_run timestamps
          nlohmann::json all_entries = nlohmann::json::array();
          for (const auto& job : jobs) {
            if (!filter_id.empty() && job.id != filter_id)
              continue;
            long long last_ms = tp_to_ms(job.last_run);
            long long next_ms = tp_to_ms(job.next_run);
            if (last_ms > 0) {
              all_entries.push_back(
                  {{"ts", last_ms},
                   {"jobId", job.id},
                   {"jobName", job.name},
                   {"status", "ok"},
                   {"runAtMs", last_ms},
                   {"nextRunAtMs", next_ms > 0 ? nlohmann::json(next_ms)
                                               : nlohmann::json(nullptr)}});
            }
          }

          int total = static_cast<int>(all_entries.size());
          int start = std::clamp(offset, 0, total);
          int end = (limit > 0) ? std::min(start + limit, total) : total;

          nlohmann::json entries = nlohmann::json::array();
          for (size_t i = static_cast<size_t>(start);
               i < static_cast<size_t>(end); ++i)
            entries.push_back(all_entries[i]);

          bool has_more = end < total;
          return {{"entries", entries},
                  {"total", total},
                  {"offset", offset},
                  {"limit", limit > 0 ? limit : total},
                  {"hasMore", has_more},
                  {"nextOffset",
                   has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
        });
  }

  // --- exec.approval.request ---
  if (exec_approval_mgr) {
    server.RegisterHandler(methods::kExecApprovalReq,
                           [exec_approval_mgr, logger](
                               const nlohmann::json& params,
                               ClientConnection& /*client*/) -> nlohmann::json {
                             std::string command = params.value("command", "");
                             if (command.empty()) {
                               throw std::runtime_error("command is required");
                             }

                             std::string cwd = params.value("cwd", "");
                             std::string agent_id = params.value("agentId", "");
                             std::string session_key =
                                 params.value("sessionKey", "");

                             auto decision = exec_approval_mgr->RequestApproval(
                                 command, cwd, agent_id, session_key);

                             std::string decision_str;
                             switch (decision) {
                               case quantclaw::ApprovalDecision::kApproved:
                                 decision_str = "approved";
                                 break;
                               case quantclaw::ApprovalDecision::kDenied:
                                 decision_str = "denied";
                                 break;
                               case quantclaw::ApprovalDecision::kPending:
                                 decision_str = "pending";
                                 break;
                               default:
                                 decision_str = "timeout";
                                 break;
                             }

                             return {{"decision", decision_str}};
                           });

    // --- exec.approvals.get ---
    server.RegisterHandler(
        methods::kExecApprovals,
        [exec_approval_mgr,
         logger](const nlohmann::json& /*params*/,
                 ClientConnection& /*client*/) -> nlohmann::json {
          const auto& cfg = exec_approval_mgr->GetConfig();

          std::string mode_str;
          switch (cfg.ask) {
            case quantclaw::AskMode::kOff:
              mode_str = "off";
              break;
            case quantclaw::AskMode::kOnMiss:
              mode_str = "on-miss";
              break;
            case quantclaw::AskMode::kAlways:
              mode_str = "always";
              break;
          }

          nlohmann::json patterns = nlohmann::json::array();
          for (const auto& p : cfg.allowlist) {
            patterns.push_back(p);
          }

          auto pending = exec_approval_mgr->PendingRequests();
          nlohmann::json pending_json = nlohmann::json::array();
          for (const auto& req : pending) {
            pending_json.push_back({{"id", req.id},
                                    {"command", req.command},
                                    {"cwd", req.cwd},
                                    {"agentId", req.agent_id},
                                    {"sessionKey", req.session_key}});
          }

          return {{"mode", mode_str},
                  {"allowlist", patterns},
                  {"pending", pending_json}};
        });
  }

  // --- models.set ---
  server.RegisterHandler(
      methods::kModelsSet,
      [agent_loop, logger](const nlohmann::json& params,
                           ClientConnection& /*client*/) -> nlohmann::json {
        std::string model = params.value("model", "");
        if (model.empty()) {
          throw std::runtime_error("model is required");
        }
        agent_loop->SetModel(model);
        return {{"ok", true}, {"model", model}};
      });

  // --- Plugin methods ---
  if (plugin_system) {
    // plugins.list
    server.RegisterHandler(
        methods::kPluginsList,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"plugins", plugin_system->Registry().ToJson()}};
        });

    // plugins.tools
    server.RegisterHandler(
        methods::kPluginsTools,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"tools", plugin_system->GetToolSchemas()}};
        });

    // plugins.call_tool
    server.RegisterHandler(
        methods::kPluginsCallTool,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string name = params.value("toolName", "");
          if (name.empty())
            throw std::runtime_error("toolName is required");
          auto args = params.value("args", nlohmann::json::object());
          return plugin_system->CallTool(name, args);
        });

    // plugins.services
    server.RegisterHandler(
        methods::kPluginsServices,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "start") {
            return plugin_system->StartService(params.value("serviceId", ""));
          }
          if (action == "stop") {
            return plugin_system->StopService(params.value("serviceId", ""));
          }
          return {{"services", plugin_system->ListServices()}};
        });

    // plugins.providers
    server.RegisterHandler(
        methods::kPluginsProviders,
        [plugin_system](const nlohmann::json& /*params*/,
                        ClientConnection& /*client*/) -> nlohmann::json {
          return {{"providers", plugin_system->ListProviders()}};
        });

    // plugins.commands
    server.RegisterHandler(
        methods::kPluginsCommands,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "execute") {
            std::string cmd = params.value("command", "");
            auto args = params.value("args", nlohmann::json::object());
            return plugin_system->ExecuteCommand(cmd, args);
          }
          return {{"commands", plugin_system->ListCommands()}};
        });

    // plugins.gateway — forward plugin-registered gateway methods
    server.RegisterHandler(
        methods::kPluginsGateway,
        [plugin_system](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string action = params.value("action", "list");
          if (action == "list") {
            return {{"methods", plugin_system->ListGatewayMethods()}};
          }
          return {{"methods", plugin_system->ListGatewayMethods()}};
        });
  }

  // ================================================================
  // Queue management RPC handlers
  // ================================================================
  if (command_queue) {
    // --- queue.status ---
    server.RegisterHandler(
        methods::kQueueStatus,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (!session_key.empty()) {
            return command_queue->SessionQueueStatus(session_key);
          }
          return command_queue->GlobalStatus();
        });

    // --- queue.configure ---
    server.RegisterHandler(
        methods::kQueueConfigure,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            // Global config update
            auto new_config = QueueConfig::FromJson(params);
            command_queue->SetConfig(new_config);
            return {{"ok", true}, {"scope", "global"}};
          }
          // Per-session config
          auto mode = QueueModeFromString(params.value("mode", "collect"));
          int debounce = params.value("debounceMs", -1);
          int cap = params.value("cap", -1);
          std::string drop = params.value("drop", "");
          command_queue->ConfigureSession(session_key, mode, debounce, cap,
                                          drop);
          return {
              {"ok", true}, {"scope", "session"}, {"sessionKey", session_key}};
        });

    // --- queue.cancel ---
    server.RegisterHandler(
        methods::kQueueCancel,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string command_id = params.value("commandId", "");
          if (command_id.empty()) {
            throw std::runtime_error("commandId is required");
          }
          bool cancelled = command_queue->Cancel(command_id);
          return {{"ok", cancelled}, {"commandId", command_id}};
        });

    // --- queue.abort ---
    server.RegisterHandler(
        methods::kQueueAbort,
        [command_queue](const nlohmann::json& params,
                        ClientConnection& /*client*/) -> nlohmann::json {
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            throw std::runtime_error("sessionKey is required");
          }
          bool aborted = command_queue->AbortSession(session_key);
          return {{"ok", aborted}, {"sessionKey", session_key}};
        });
  }

  // --- memory.status ---
  {
    auto workspace =
        std::filesystem::path(quantclaw::platform::home_directory()) /
        ".quantclaw/agents/main/workspace";

    server.RegisterHandler(
        methods::kMemoryStatus,
        [workspace, logger](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
          quantclaw::MemorySearch search(logger);
          search.IndexDirectory(workspace);
          return search.Stats();
        });

    // --- memory.search ---
    server.RegisterHandler(
        methods::kMemorySearch,
        [workspace, logger](const nlohmann::json& params,
                            ClientConnection& /*client*/) -> nlohmann::json {
          std::string query = params.value("query", "");
          int max_results = params.value("maxResults", 10);
          if (query.empty()) {
            throw std::runtime_error("query is required");
          }
          quantclaw::MemorySearch search(logger);
          search.IndexDirectory(workspace);
          auto results = search.Search(query, max_results);
          nlohmann::json arr = nlohmann::json::array();
          for (const auto& r : results) {
            arr.push_back({{"source", r.source},
                           {"content", r.content},
                           {"score", r.score},
                           {"lineNumber", r.line_number}});
          }
          return arr;
        });
  }

  // ================================================================
  // UI compatibility handlers — missing in original implementation
  // ================================================================

  // --- agent.identity.get ---
  // Called on every UI connect to show assistant name/avatar.
  server.RegisterHandler("agent.identity.get",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return {{"agentId", "main"},
                                   {"name", "QuantClaw Agent"},
                                   {"avatar", ""},
                                   {"emoji", "\xF0\x9F\xA6\x9E"}};
                         });

  // --- node.list ---
  // QuantClaw is a single-node deployment; return empty list.
  server.RegisterHandler("node.list",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return nlohmann::json::array();
                         });

  // --- device.pair.list ---
  // Device pairing not implemented; return empty list so UI doesn't hang.
  server.RegisterHandler("device.pair.list",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return nlohmann::json::array();
                         });

  // --- logs.tail ---
  // Return recent lines from the gateway log file.
  server.RegisterHandler(
      "logs.tail",
      [logger, log_file_path](const nlohmann::json& params,
                              ClientConnection& /*client*/) -> nlohmann::json {
        int req_limit = params.value("limit", 200);
        int max_bytes = params.value("maxBytes", 512 * 1024);
        long long cursor = params.value("cursor", 0LL);
        (void)max_bytes;
        (void)cursor;

        nlohmann::json lines = nlohmann::json::array();
        long long new_cursor = cursor;
        bool truncated = false;

        // Normalize the path and verify its structure before opening
        // (guards against $HOME containing '..' traversal components).
        namespace fs = std::filesystem;
        fs::path safe = fs::path(log_file_path).lexically_normal();
        bool path_ok = !safe.empty() && safe.filename() == "gateway.log" &&
                       safe.parent_path().filename() == "logs" &&
                       fs::exists(safe);
        if (path_ok) {
          std::ifstream ifs(safe);
          if (ifs.is_open()) {
            std::vector<std::string> all_lines;
            std::string line;
            while (std::getline(ifs, line)) {
              all_lines.push_back(line);
            }
            int total = static_cast<int>(all_lines.size());
            int start = std::max(0, total - req_limit);
            for (size_t i = static_cast<size_t>(start);
                 i < static_cast<size_t>(total); ++i) {
              lines.push_back(all_lines[i]);
            }
            new_cursor = total;
            truncated = start > 0;
          }
        }

        return {{"file", log_file_path},
                {"cursor", new_cursor},
                {"lines", lines},
                {"truncated", truncated}};
      });

  // --- config.schema ---
  // Return a minimal JSON schema for the config form.
  server.RegisterHandler(
      "config.schema",
      [](const nlohmann::json& /*params*/,
         ClientConnection& /*client*/) -> nlohmann::json {
        nlohmann::json schema = {
            {"type", "object"},
            {"properties",
             {{"agent",
               {{"type", "object"},
                {"properties",
                 {{"model", {{"type", "string"}}},
                  {"maxIterations",
                   {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
                  {"temperature",
                   {{"type", "number"}, {"minimum", 0}, {"maximum", 2}}},
                  {"maxTokens", {{"type", "integer"}, {"minimum", 1}}},
                  {"thinking",
                   {{"type", "string"},
                    {"enum", nlohmann::json::array(
                                 {"off", "low", "medium", "high"})}}}}}}},
              {"gateway",
               {{"type", "object"},
                {"properties",
                 {{"port",
                   {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}},
                  {"bind", {{"type", "string"}}}}}}}}}};
        nlohmann::json ui_hints = {
            {"agent.model", {{"label", "Model"}, {"group", "Agent"}}},
            {"agent.maxIterations",
             {{"label", "Max Iterations"}, {"group", "Agent"}}},
            {"agent.temperature",
             {{"label", "Temperature"},
              {"group", "Agent"},
              {"advanced", true}}},
            {"agent.thinking",
             {{"label", "Thinking Mode"}, {"group", "Agent"}}},
            {"gateway.port", {{"label", "Port"}, {"group", "Gateway"}}},
            {"gateway.bind",
             {{"label", "Bind Address"}, {"group", "Gateway"}}}};
        return {{"schema", schema},
                {"uiHints", ui_hints},
                {"version", "1"},
                {"generatedAt", ""}};
      });

  // --- cron.status ---
  if (cron_scheduler) {
    server.RegisterHandler(
        "cron.status",
        [cron_scheduler](const nlohmann::json& /*params*/,
                         ClientConnection& /*client*/) -> nlohmann::json {
          auto jobs = cron_scheduler->ListJobs();
          int enabled_count = 0;
          long long next_wake = 0;
          for (const auto& job : jobs) {
            if (job.enabled) {
              ++enabled_count;
              auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            job.next_run.time_since_epoch())
                            .count();
              if (ms > 0 && (next_wake == 0 || ms < next_wake)) {
                next_wake = ms;
              }
            }
          }
          return {{"enabled", enabled_count > 0},
                  {"jobs", static_cast<int>(jobs.size())},
                  {"nextWakeAtMs", next_wake > 0 ? nlohmann::json(next_wake)
                                                 : nlohmann::json(nullptr)}};
        });
  }

  // --- skills.update ---
  // Enable/disable a skill or save its API key.
  if (skill_loader) {
    server.RegisterHandler(
        "skills.update",
        [logger](const nlohmann::json& params,
                 ClientConnection& /*client*/) -> nlohmann::json {
          std::string skill_key = params.value("skillKey", "");
          if (skill_key.empty()) {
            throw std::runtime_error("skillKey is required");
          }
          // Persist enable/disable flag via env or config file is not yet
          // implemented; acknowledge the request so the UI doesn't show an
          // error.
          bool enabled = params.value("enabled", true);
          logger->info("skills.update: skillKey={} enabled={}", skill_key,
                       enabled);
          return {{"ok", true}, {"skillKey", skill_key}};
        });
  }

  // --- sessions.usage ---
  // Aggregate token usage from session histories for a date range.
  server.RegisterHandler(
      "sessions.usage",
      [session_manager,
       &config](const nlohmann::json& params,
                ClientConnection& /*client*/) -> nlohmann::json {
        std::string start_date = params.value("startDate", "");
        std::string end_date = params.value("endDate", "");

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        // Aggregate totals across all sessions
        long long total_input = 0, total_output = 0;
        nlohmann::json session_entries = nlohmann::json::array();

        auto sessions = session_manager->ListSessions();
        for (const auto& s : sessions) {
          auto history = session_manager->GetHistory(s.session_key, -1);
          long long in_tok = 0, out_tok = 0;
          for (const auto& msg : history) {
            if (msg.usage) {
              in_tok += msg.usage->input_tokens;
              out_tok += msg.usage->output_tokens;
            }
          }
          total_input += in_tok;
          total_output += out_tok;

          session_entries.push_back({{"key", s.session_key},
                                     {"label", s.display_name},
                                     {"model", config.agent.model},
                                     {"usage",
                                      {{"input", in_tok},
                                       {"output", out_tok},
                                       {"cacheRead", 0},
                                       {"cacheWrite", 0},
                                       {"totalTokens", in_tok + out_tok},
                                       {"totalCost", 0.0},
                                       {"missingCostEntries", 0}}}});
        }

        nlohmann::json zero_totals = {
            {"input", total_input},
            {"output", total_output},
            {"cacheRead", 0},
            {"cacheWrite", 0},
            {"totalTokens", total_input + total_output},
            {"totalCost", 0.0},
            {"inputCost", 0.0},
            {"outputCost", 0.0},
            {"cacheReadCost", 0.0},
            {"cacheWriteCost", 0.0},
            {"missingCostEntries", 0}};

        return {{"updatedAt", now_ms},
                {"startDate", start_date},
                {"endDate", end_date},
                {"sessions", session_entries},
                {"totals", zero_totals},
                {"aggregates",
                 {{"messages",
                   {{"total", 0},
                    {"user", 0},
                    {"assistant", 0},
                    {"toolCalls", 0},
                    {"toolResults", 0},
                    {"errors", 0}}},
                  {"tools",
                   {{"totalCalls", 0},
                    {"uniqueTools", 0},
                    {"tools", nlohmann::json::array()}}},
                  {"byModel", nlohmann::json::array()},
                  {"byProvider", nlohmann::json::array()},
                  {"byAgent", nlohmann::json::array()},
                  {"byChannel", nlohmann::json::array()},
                  {"daily", nlohmann::json::array()}}}};
      });

  // --- usage.cost ---
  // Return daily cost summary (costs are zero since we don't track model
  // pricing).
  server.RegisterHandler(
      "usage.cost",
      [](const nlohmann::json& params,
         ClientConnection& /*client*/) -> nlohmann::json {
        std::string start_date = params.value("startDate", "");
        std::string end_date = params.value("endDate", "");
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        nlohmann::json zero_totals = {{"input", 0},
                                      {"output", 0},
                                      {"cacheRead", 0},
                                      {"cacheWrite", 0},
                                      {"totalTokens", 0},
                                      {"totalCost", 0.0},
                                      {"inputCost", 0.0},
                                      {"outputCost", 0.0},
                                      {"cacheReadCost", 0.0},
                                      {"cacheWriteCost", 0.0},
                                      {"missingCostEntries", 0}};
        return {{"updatedAt", now_ms},
                {"days", 0},
                {"daily", nlohmann::json::array()},
                {"totals", zero_totals}};
      });

  // --- sessions.usage.timeseries ---
  // Return empty time series; UI silently fails on this.
  server.RegisterHandler("sessions.usage.timeseries",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return {{"sessionId", nullptr},
                                   {"points", nlohmann::json::array()}};
                         });

  // --- sessions.usage.logs ---
  // Return empty logs; UI silently fails on this.
  server.RegisterHandler("sessions.usage.logs",
                         [](const nlohmann::json& /*params*/,
                            ClientConnection& /*client*/) -> nlohmann::json {
                           return {{"logs", nlohmann::json::array()}};
                         });

  int handler_count = 24;  // base handlers (22 + 2 memory)
  if (reload_fn)
    handler_count++;
  if (skill_loader)
    handler_count += 3;  // status, install, update
  if (cron_scheduler)
    handler_count += 7;  // list, add, remove, update, run, runs, status
  if (exec_approval_mgr)
    handler_count += 2;
  if (plugin_system)
    handler_count += 7;
  if (command_queue)
    handler_count += 4;
  handler_count +=
      10;  // ui compat: identity, node.list, device.pair.list, logs.tail,
           //            config.schema, sessions.usage, usage.cost,
           //            sessions.usage.timeseries, sessions.usage.logs
  logger->info("Registered {} RPC handlers", handler_count);
}

}  // namespace quantclaw::gateway
