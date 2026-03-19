// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/web/api_routes.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "quantclaw/config.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/plugins/plugin_system.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/web/web_server.hpp"

namespace quantclaw::web {

// --- Helpers ---

static void json_ok(httplib::Response& res, const nlohmann::json& data) {
  res.status = 200;
  res.set_content(data.dump(), "application/json");
}

static void json_error(httplib::Response& res, int status,
                       const std::string& message) {
  res.status = status;
  nlohmann::json err = {{"error", message}, {"status", status}};
  res.set_content(err.dump(), "application/json");
}

static std::string generate_openai_session_key() {
  thread_local static std::mt19937 gen(std::random_device{}());
  thread_local static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << "v1-chat:" << std::hex << dist(gen);
  return ss.str();
}

// --- Route registration ---

void register_api_routes(
    WebServer& server,
    const std::shared_ptr<quantclaw::SessionManager>& session_manager,
    const std::shared_ptr<quantclaw::AgentLoop>& agent_loop,
    const std::shared_ptr<quantclaw::PromptBuilder>& prompt_builder,
    const std::shared_ptr<quantclaw::ToolRegistry>& /*tool_registry*/,
    const quantclaw::QuantClawConfig& config,
    quantclaw::gateway::GatewayServer& gateway_server,
    const std::shared_ptr<spdlog::logger>& logger,
    const std::function<void()>& reload_fn,
    quantclaw::PluginSystem* plugin_system) {
  // --- GET /api/health ---
  server.AddRawRoute(
      "/api/health", "GET",
      [&gateway_server](const httplib::Request&, httplib::Response& res) {
        json_ok(res, {{"status", "ok"},
                      {"uptime", gateway_server.GetUptimeSeconds()},
                      {"version", "0.2.0"}});
      });

  // --- GET /api/status ---
  server.AddRawRoute(
      "/api/status", "GET",
      [&gateway_server, session_manager](const httplib::Request&,
                                         httplib::Response& res) {
        auto sessions = session_manager->ListSessions();
        json_ok(res, {{"running", true},
                      {"port", gateway_server.GetPort()},
                      {"connections", gateway_server.GetConnectionCount()},
                      {"uptime", gateway_server.GetUptimeSeconds()},
                      {"sessions", sessions.size()},
                      {"version", "0.2.0"}});
      });

  // --- GET /api/config ---
  server.AddRawRoute(
      "/api/config", "GET",
      [&config](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.get_param_value("path");

        if (path.empty()) {
          json_ok(res, {{"agent",
                         {{"model", config.agent.model},
                          {"maxIterations", config.agent.max_iterations},
                          {"temperature", config.agent.temperature}}},
                        {"gateway",
                         {{"port", config.gateway.port},
                          {"bind", config.gateway.bind}}}});
          return;
        }

        if (path == "gateway.port") {
          json_ok(res, config.gateway.port);
          return;
        }
        if (path == "gateway.bind") {
          json_ok(res, config.gateway.bind);
          return;
        }
        if (path == "agent.model") {
          json_ok(res, config.agent.model);
          return;
        }
        if (path == "agent.maxIterations") {
          json_ok(res, config.agent.max_iterations);
          return;
        }
        if (path == "agent.temperature") {
          json_ok(res, config.agent.temperature);
          return;
        }

        json_error(res, 400, "Unknown config path: " + path);
      });

  // --- POST /api/agent/request ---
  server.AddRawRoute(
      "/api/agent/request", "POST",
      [session_manager, agent_loop, prompt_builder,
       logger](const httplib::Request& req, httplib::Response& res) {
        try {
          auto params = nlohmann::json::parse(req.body);
          std::string session_key =
              params.value("sessionKey", "agent:main:main");
          std::string message = params.value("message", "");

          if (message.empty()) {
            json_error(res, 400, "message is required");
            return;
          }

          // Get or create session
          session_manager->GetOrCreate(session_key, "", "api");

          // Auto-generate display_name from first message
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

          // Convert to LLM Messages
          std::vector<quantclaw::Message> llm_history;
          for (const auto& smsg : history) {
            quantclaw::Message m;
            m.role = smsg.role;
            m.content = smsg.content;
            llm_history.push_back(m);
          }
          if (!llm_history.empty()) {
            llm_history.pop_back();
          }

          // Non-streaming call
          auto new_messages =
              agent_loop->ProcessMessage(message, llm_history, system_prompt);

          // Extract final assistant text
          std::string final_response;
          for (const auto& msg : new_messages) {
            if (msg.role == "assistant") {
              for (const auto& block : msg.content) {
                if (block.type == "text") {
                  final_response = block.text;
                }
              }
            }
          }

          // Persist all new messages
          for (const auto& msg : new_messages) {
            quantclaw::SessionMessage smsg;
            smsg.role = msg.role;
            smsg.content = msg.content;
            session_manager->AppendMessage(session_key, smsg);
          }

          json_ok(res,
                  {{"sessionKey", session_key}, {"response", final_response}});
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
          json_error(res, 500, e.what());
        }
      });

  // --- POST /api/agent/stream (SSE) ---
  // Server-Sent Events endpoint: streams agent events in real-time.
  // Request body: {"sessionKey": "...", "message": "..."}
  // Response: text/event-stream with events:
  //   event: agent.text_delta   data: {"text": "..."}
  //   event: agent.tool_use     data: {"id": "...", "name": "...", "input":
  //   {...}} event: agent.tool_result  data: {"tool_use_id": "...", "content":
  //   "..."} event: agent.message_end  data: {"content": "..."} event: done
  //   data: {"sessionKey": "...", "response": "..."}
  server.AddRawRoute(
      "/api/agent/stream", "POST",
      [session_manager, agent_loop, prompt_builder,
       logger](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json params;
        try {
          params = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
          return;
        }

        std::string session_key = params.value("sessionKey", "agent:main:main");
        std::string message = params.value("message", "");
        if (message.empty()) {
          json_error(res, 400, "message is required");
          return;
        }

        // Shared state for bridging the push-based agent callback to
        // httplib's pull-based chunked content provider.
        struct StreamState {
          std::mutex mu;
          std::condition_variable cv;
          std::queue<std::string> chunks;
          bool finished = false;
        };
        auto state = std::make_shared<StreamState>();

        // Prepare session and history before spawning the worker thread
        session_manager->GetOrCreate(session_key, "", "api");

        auto sessions = session_manager->ListSessions();
        for (const auto& s : sessions) {
          if (s.session_key == session_key && s.display_name == session_key) {
            session_manager->UpdateDisplayName(session_key,
                                               message.substr(0, 50));
            break;
          }
        }

        session_manager->AppendMessage(session_key, "user", message);
        std::string system_prompt = prompt_builder->BuildFull();

        auto history = session_manager->GetHistory(session_key, 50);
        std::vector<quantclaw::Message> llm_history;
        llm_history.reserve(history.size());
        for (const auto& smsg : history) {
          quantclaw::Message m;
          m.role = smsg.role;
          m.content = smsg.content;
          llm_history.push_back(std::move(m));
        }
        if (!llm_history.empty()) {
          llm_history.pop_back();
        }

        // Start agent processing in a background thread
        std::thread worker([state, session_manager, agent_loop, message,
                            llm_history = std::move(llm_history), system_prompt,
                            session_key, logger]() {
          try {
            auto new_messages = agent_loop->ProcessMessageStream(
                message, llm_history, system_prompt,
                [&state](const quantclaw::AgentEvent& event) {
                  std::string sse = "event: " + event.type +
                                    "\ndata: " + event.data.dump() + "\n\n";
                  std::lock_guard<std::mutex> lock(state->mu);
                  state->chunks.push(std::move(sse));
                  state->cv.notify_one();
                });

            // Persist new messages to the session transcript
            for (const auto& msg : new_messages) {
              quantclaw::SessionMessage smsg;
              smsg.role = msg.role;
              smsg.content = msg.content;
              session_manager->AppendMessage(session_key, smsg);
            }

            // Extract final response text
            std::string final_response;
            for (const auto& msg : new_messages) {
              if (msg.role == "assistant") {
                for (const auto& block : msg.content) {
                  if (block.type == "text") {
                    final_response = block.text;
                  }
                }
              }
            }

            // Send terminal "done" event
            nlohmann::json done_data = {{"sessionKey", session_key},
                                        {"response", final_response}};
            std::string done_sse =
                "event: done\ndata: " + done_data.dump() + "\n\n";

            std::lock_guard<std::mutex> lock(state->mu);
            state->chunks.push(std::move(done_sse));
            state->finished = true;
            state->cv.notify_one();
          } catch (const std::exception& e) {
            nlohmann::json err = {{"error", e.what()}};
            std::string err_sse = "event: error\ndata: " + err.dump() + "\n\n";

            std::lock_guard<std::mutex> lock(state->mu);
            state->chunks.push(std::move(err_sse));
            state->finished = true;
            state->cv.notify_one();
          }
        });
        worker.detach();

        // SSE response headers
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");

        res.set_chunked_content_provider(
            "text/event-stream",
            [state](size_t /*offset*/, httplib::DataSink& sink) -> bool {
              std::unique_lock<std::mutex> lock(state->mu);
              state->cv.wait(lock, [&state] {
                return !state->chunks.empty() || state->finished;
              });

              while (!state->chunks.empty()) {
                auto& chunk = state->chunks.front();
                sink.write(chunk.data(), chunk.size());
                state->chunks.pop();
              }

              if (state->finished) {
                sink.done();
                return false;
              }
              return true;
            });
      });

  // --- POST /api/agent/stop ---
  server.AddRawRoute(
      "/api/agent/stop", "POST",
      [agent_loop](const httplib::Request&, httplib::Response& res) {
        agent_loop->Stop();
        json_ok(res, {{"ok", true}});
      });

  // --- GET /api/sessions ---
  server.AddRawRoute(
      "/api/sessions", "GET",
      [session_manager](const httplib::Request& req, httplib::Response& res) {
        int limit = 50;
        int offset = 0;
        if (req.has_param("limit")) {
          try {
            limit = std::stoi(req.get_param_value("limit"));
          } catch (const std::exception&) {}
        }
        if (req.has_param("offset")) {
          try {
            offset = std::stoi(req.get_param_value("offset"));
          } catch (const std::exception&) {}
        }

        auto sessions = session_manager->ListSessions();
        int total = static_cast<int>(sessions.size());
        if (limit < 0)
          limit = 0;
        if (offset < 0)
          offset = 0;
        if (offset > total)
          offset = total;
        // Clamp limit to avoid integer overflow in offset + limit.
        if (limit > total - offset)
          limit = total - offset;
        nlohmann::json result = nlohmann::json::array();
        int end = offset + limit;
        for (int i = offset; i < end; ++i) {
          result.push_back({{"key", sessions[i].session_key},
                            {"id", sessions[i].session_id},
                            {"updatedAt", sessions[i].updated_at},
                            {"createdAt", sessions[i].created_at},
                            {"displayName", sessions[i].display_name},
                            {"channel", sessions[i].channel}});
        }
        json_ok(res, result);
      });

  // --- GET /api/sessions/history ---
  server.AddRawRoute(
      "/api/sessions/history", "GET",
      [session_manager](const httplib::Request& req, httplib::Response& res) {
        std::string session_key = req.get_param_value("sessionKey");
        if (session_key.empty()) {
          json_error(res, 400, "sessionKey query parameter is required");
          return;
        }
        int limit = -1;
        if (req.has_param("limit")) {
          try {
            limit = std::stoi(req.get_param_value("limit"));
          } catch (const std::exception&) {}
        }

        auto history = session_manager->GetHistory(session_key, limit);
        nlohmann::json result = nlohmann::json::array();
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
          result.push_back(entry);
        }
        json_ok(res, result);
      });

  // --- POST /api/sessions/delete ---
  server.AddRawRoute(
      "/api/sessions/delete", "POST",
      [session_manager](const httplib::Request& req, httplib::Response& res) {
        try {
          auto params = nlohmann::json::parse(req.body);
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            json_error(res, 400, "sessionKey is required");
            return;
          }
          bool deleted = session_manager->DeleteSession(session_key);
          json_ok(res, {{"ok", true}, {"deleted", deleted}});
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
          json_error(res, 500, e.what());
        }
      });

  // --- POST /api/sessions/reset ---
  server.AddRawRoute(
      "/api/sessions/reset", "POST",
      [session_manager](const httplib::Request& req, httplib::Response& res) {
        try {
          auto params = nlohmann::json::parse(req.body);
          std::string session_key = params.value("sessionKey", "");
          if (session_key.empty()) {
            json_error(res, 400, "sessionKey is required");
            return;
          }
          session_manager->ResetSession(session_key);
          json_ok(res, {{"ok", true}});
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
          json_error(res, 500, e.what());
        }
      });

  // --- POST /api/config/reload ---
  if (reload_fn) {
    server.AddRawRoute(
        "/api/config/reload", "POST",
        [reload_fn, logger](const httplib::Request&, httplib::Response& res) {
          try {
            reload_fn();
            json_ok(res, {{"ok", true}});
          } catch (const std::exception& e) {
            json_error(res, 500, e.what());
          }
        });
  }

  // --- POST /v1/chat/completions (OpenAI-compatible endpoint) ---
  server.AddRawRoute(
      "/v1/chat/completions", "POST",
      [agent_loop, session_manager, logger](const httplib::Request& req,
                                            httplib::Response& res) {
        try {
          auto body = nlohmann::json::parse(req.body);

          // Extract messages (OpenAI format)
          if (!body.contains("messages") || !body["messages"].is_array()) {
            json_error(res, 400, "messages array is required");
            return;
          }

          // Extract the last user message
          std::string user_message;
          std::vector<quantclaw::Message> history;
          for (const auto& msg : body["messages"]) {
            std::string role = msg.value("role", "");
            std::string content = msg.value("content", "");
            if (role == "system" || role == "user" || role == "assistant") {
              history.emplace_back(role, content);
            }
          }

          // The last user message is what we process
          if (!history.empty() && history.back().role == "user") {
            user_message = history.back().text();
            history.pop_back();
          } else {
            json_error(res, 400, "Last message must be from user");
            return;
          }

          // Extract system prompt from messages
          std::string system_prompt;
          std::vector<quantclaw::Message> llm_history;
          for (const auto& msg : history) {
            if (msg.role == "system") {
              system_prompt += msg.text();
            } else {
              llm_history.push_back(msg);
            }
          }

          bool stream = body.value("stream", false);
          std::string model = body.value("model", "default");

          // Extract or generate session key (supports both body field and
          // X-Session-Key header)
          std::string session_key;
          if (body.contains("sessionKey") && body["sessionKey"].is_string()) {
            session_key = body["sessionKey"].get<std::string>();
          } else if (req.has_header("X-Session-Key")) {
            session_key = req.get_header_value("X-Session-Key");
          } else {
            session_key = generate_openai_session_key();
          }

          if (stream) {
            // Streaming response (SSE format matching OpenAI)
            struct StreamState {
              std::mutex mu;
              std::condition_variable cv;
              std::queue<std::string> chunks;
              bool finished = false;
            };
            auto state = std::make_shared<StreamState>();
            std::string resp_id =
                "chatcmpl-qc-" + std::to_string(std::chrono::system_clock::now()
                                                    .time_since_epoch()
                                                    .count());

            std::thread worker([state, agent_loop, user_message,
                                llm_history = std::move(llm_history),
                                system_prompt, model, resp_id, session_key,
                                logger]() {
              try {
                agent_loop->ProcessMessageStream(
                    user_message, llm_history, system_prompt,
                    [&state, &model,
                     &resp_id](const quantclaw::AgentEvent& event) {
                      if (event.type == "agent.text_delta" &&
                          event.data.contains("text")) {
                        nlohmann::json chunk;
                        chunk["id"] = resp_id;
                        chunk["object"] = "chat.completion.chunk";
                        chunk["created"] =
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now()
                                    .time_since_epoch())
                                .count();
                        chunk["model"] = model;
                        chunk["choices"] = nlohmann::json::array(
                            {{{"index", 0},
                              {"delta", {{"content", event.data["text"]}}},
                              {"finish_reason", nullptr}}});
                        std::string sse = "data: " + chunk.dump() + "\n\n";
                        std::lock_guard<std::mutex> lock(state->mu);
                        state->chunks.push(std::move(sse));
                        state->cv.notify_one();
                      }
                    },
                    session_key);

                // Send [DONE] marker
                {
                  std::lock_guard<std::mutex> lock(state->mu);
                  state->chunks.push("data: [DONE]\n\n");
                  state->finished = true;
                  state->cv.notify_one();
                }
              } catch (const std::exception& e) {
                logger->error("OpenAI stream error: {}", e.what());
                std::lock_guard<std::mutex> lock(state->mu);
                state->finished = true;
                state->cv.notify_one();
              }
            });
            worker.detach();

            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("X-Accel-Buffering", "no");

            res.set_chunked_content_provider(
                "text/event-stream",
                [state](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                  std::unique_lock<std::mutex> lock(state->mu);
                  state->cv.wait(lock, [&state] {
                    return !state->chunks.empty() || state->finished;
                  });
                  while (!state->chunks.empty()) {
                    auto& c = state->chunks.front();
                    sink.write(c.data(), c.size());
                    state->chunks.pop();
                  }
                  if (state->finished) {
                    sink.done();
                    return false;
                  }
                  return true;
                });
          } else {
            // Non-streaming response
            // Track usage delta for this request (per-session to avoid race
            // conditions)
            auto usage_acc = agent_loop->GetUsageAccumulator();
            auto usage_before = usage_acc
                                    ? usage_acc->GetSession(session_key)
                                    : quantclaw::UsageAccumulator::Stats{};

            auto new_messages = agent_loop->ProcessMessage(
                user_message, llm_history, system_prompt, session_key);

            auto usage_after = usage_acc ? usage_acc->GetSession(session_key)
                                         : quantclaw::UsageAccumulator::Stats{};

            std::string final_response;
            for (const auto& msg : new_messages) {
              if (msg.role == "assistant") {
                final_response = msg.text();
              }
            }

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            // Calculate token deltas for this request
            int64_t prompt_tokens =
                usage_after.input_tokens - usage_before.input_tokens;
            int64_t completion_tokens =
                usage_after.output_tokens - usage_before.output_tokens;
            int64_t total_tokens = prompt_tokens + completion_tokens;

            nlohmann::json response;
            response["id"] = "chatcmpl-qc-" + std::to_string(now);
            response["object"] = "chat.completion";
            response["created"] = now;
            response["model"] = model;
            response["choices"] = nlohmann::json::array(
                {{{"index", 0},
                  {"message",
                   {{"role", "assistant"}, {"content", final_response}}},
                  {"finish_reason", "stop"}}});
            response["usage"] = {{"prompt_tokens", prompt_tokens},
                                 {"completion_tokens", completion_tokens},
                                 {"total_tokens", total_tokens}};

            json_ok(res, response);
          }
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
          json_error(res, 500, e.what());
        }
      });

  // --- GET /v1/models (OpenAI-compatible model list) ---
  server.AddRawRoute(
      "/v1/models", "GET",
      [&config](const httplib::Request&, httplib::Response& res) {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

        nlohmann::json models = nlohmann::json::array();
        models.push_back({{"id", config.agent.model},
                          {"object", "model"},
                          {"created", now},
                          {"owned_by", "quantclaw"}});

        json_ok(res, {{"object", "list"}, {"data", models}});
      });

  // --- GET /api/channels ---
  server.AddRawRoute("/api/channels", "GET",
                     [](const httplib::Request&, httplib::Response& res) {
                       json_ok(res,
                               nlohmann::json::array({{{"name", "cli"},
                                                       {"type", "cli"},
                                                       {"status", "active"}}}));
                     });

  // --- POST /api/channel/message ---
  // Generic webhook endpoint for external integrations (e.g. Feishu, DingTalk,
  // custom bots) Accepts: {"channel": "discord", "senderId": "...",
  // "channelId": "...", "message": "..."} Returns: {"response": "...",
  // "sessionKey": "..."}
  server.AddRawRoute(
      "/api/channel/message", "POST",
      [session_manager, agent_loop, prompt_builder,
       logger](const httplib::Request& req, httplib::Response& res) {
        try {
          auto params = nlohmann::json::parse(req.body);
          std::string channel = params.value("channel", "webhook");
          std::string sender_id = params.value("senderId", "anonymous");
          std::string channel_id = params.value("channelId", "default");
          std::string message = params.value("message", "");

          if (message.empty()) {
            json_error(res, 400, "message is required");
            return;
          }

          // Session key: channel:<platform>:<channelId>
          std::string session_key = "channel:" + channel + ":" + channel_id;

          // Get or create session
          session_manager->GetOrCreate(session_key, "", channel);

          // Auto-generate display_name
          auto sessions = session_manager->ListSessions();
          for (const auto& s : sessions) {
            if (s.session_key == session_key && s.display_name == session_key) {
              session_manager->UpdateDisplayName(
                  session_key, channel + ": " + message.substr(0, 40));
              break;
            }
          }

          // Append user message
          session_manager->AppendMessage(session_key, "user", message);

          // Build system prompt
          std::string system_prompt = prompt_builder->BuildFull();

          // Load history
          auto history = session_manager->GetHistory(session_key, 50);
          std::vector<quantclaw::Message> llm_history;
          for (const auto& smsg : history) {
            quantclaw::Message m;
            m.role = smsg.role;
            m.content = smsg.content;
            llm_history.push_back(m);
          }
          if (!llm_history.empty()) {
            llm_history.pop_back();
          }

          // Non-streaming call
          auto new_messages =
              agent_loop->ProcessMessage(message, llm_history, system_prompt);

          // Extract final assistant text
          std::string final_response;
          for (const auto& msg : new_messages) {
            if (msg.role == "assistant") {
              for (const auto& block : msg.content) {
                if (block.type == "text") {
                  final_response = block.text;
                }
              }
            }
          }

          // Persist
          for (const auto& msg : new_messages) {
            quantclaw::SessionMessage smsg;
            smsg.role = msg.role;
            smsg.content = msg.content;
            session_manager->AppendMessage(session_key, smsg);
          }

          json_ok(res,
                  {{"sessionKey", session_key}, {"response", final_response}});
        } catch (const nlohmann::json::exception& e) {
          json_error(res, 400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
          json_error(res, 500, e.what());
        }
      });

  // --- Plugin HTTP routes (forwarded to sidecar) ---
  int route_count = reload_fn ? 15 : 14;

  if (plugin_system) {
    // GET /api/plugins — list plugins
    server.AddRawRoute("/api/plugins", "GET",
                       [plugin_system](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
                         auto plugins_json = plugin_system->Registry().ToJson();
                         json_ok(res, {{"plugins", plugins_json}});
                       });

    // GET /api/plugins/tools — list plugin tools
    server.AddRawRoute("/api/plugins/tools", "GET",
                       [plugin_system](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
                         auto tools = plugin_system->GetToolSchemas();
                         json_ok(res, {{"tools", tools}});
                       });

    // POST /api/plugins/tools/:name — call a plugin tool
    server.AddRawRoute(
        "/api/plugins/tools/(.*)", "POST",
        [plugin_system](const httplib::Request& req, httplib::Response& res) {
          try {
            // Extract tool name from path
            auto path = req.path;
            auto prefix = std::string("/api/plugins/tools/");
            std::string tool_name = path.substr(prefix.size());
            if (tool_name.empty()) {
              json_error(res, 400, "Tool name is required");
              return;
            }
            nlohmann::json args = nlohmann::json::object();
            if (!req.body.empty()) {
              args = nlohmann::json::parse(req.body);
            }
            auto result = plugin_system->CallTool(tool_name, args);
            json_ok(res, result);
          } catch (const std::exception& e) {
            json_error(res, 500, e.what());
          }
        });

    // GET /api/plugins/services — list services
    server.AddRawRoute("/api/plugins/services", "GET",
                       [plugin_system](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
                         auto services = plugin_system->ListServices();
                         json_ok(res, {{"services", services}});
                       });

    // GET /api/plugins/providers — list providers
    server.AddRawRoute("/api/plugins/providers", "GET",
                       [plugin_system](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
                         auto providers = plugin_system->ListProviders();
                         json_ok(res, {{"providers", providers}});
                       });

    // GET /api/plugins/commands — list commands
    server.AddRawRoute("/api/plugins/commands", "GET",
                       [plugin_system](const httplib::Request& /*req*/,
                                       httplib::Response& res) {
                         auto commands = plugin_system->ListCommands();
                         json_ok(res, {{"commands", commands}});
                       });

    // ANY /plugins/* — forward to sidecar plugin HTTP handlers
    server.AddRawRoute(
        "/plugins/(.*)", "GET",
        [plugin_system](const httplib::Request& req, httplib::Response& res) {
          auto result = plugin_system->HandleHttp(req.method, req.path,
                                                  nlohmann::json::object(), {});
          int status = result.value("status", 200);
          res.status = status;
          if (result.contains("body")) {
            res.set_content(result["body"].dump(), "application/json");
          }
        });

    server.AddRawRoute(
        "/plugins/(.*)", "POST",
        [plugin_system](const httplib::Request& req, httplib::Response& res) {
          nlohmann::json body = nlohmann::json::object();
          if (!req.body.empty()) {
            try {
              body = nlohmann::json::parse(req.body);
            } catch (...) {}
          }
          auto result =
              plugin_system->HandleHttp(req.method, req.path, body, {});
          int status = result.value("status", 200);
          res.status = status;
          if (result.contains("body")) {
            res.set_content(result["body"].dump(), "application/json");
          }
        });

    route_count += 8;
    logger->info("Registered plugin HTTP routes (8 endpoints)");
  }

  logger->info("Registered {} HTTP API routes", route_count);
}

}  // namespace quantclaw::web
