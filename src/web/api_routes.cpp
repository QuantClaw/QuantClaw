#include "quantclaw/web/api_routes.hpp"
#include "quantclaw/web/web_server.hpp"
#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/config.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace quantclaw::web {

// --- Helpers ---

static void json_ok(httplib::Response& res, const nlohmann::json& data) {
    res.status = 200;
    res.set_content(data.dump(), "application/json");
}

static void json_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    nlohmann::json err = {{"error", message}, {"status", status}};
    res.set_content(err.dump(), "application/json");
}

// --- Route registration ---

void register_api_routes(
    WebServer& server,
    std::shared_ptr<quantclaw::SessionManager> session_manager,
    std::shared_ptr<quantclaw::AgentLoop> agent_loop,
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
    std::shared_ptr<quantclaw::ToolRegistry> /*tool_registry*/,
    const quantclaw::QuantClawConfig& config,
    quantclaw::gateway::GatewayServer& gateway_server,
    std::shared_ptr<spdlog::logger> logger,
    std::function<void()> reload_fn)
{
    // --- GET /api/health ---
    server.add_raw_route("/api/health", "GET",
        [&gateway_server](const httplib::Request&, httplib::Response& res) {
            json_ok(res, {
                {"status", "ok"},
                {"uptime", gateway_server.get_uptime_seconds()},
                {"version", "0.2.0"}
            });
        }
    );

    // --- GET /api/status ---
    server.add_raw_route("/api/status", "GET",
        [&gateway_server, session_manager](const httplib::Request&, httplib::Response& res) {
            auto sessions = session_manager->list_sessions();
            json_ok(res, {
                {"running", true},
                {"port", gateway_server.get_port()},
                {"connections", gateway_server.get_connection_count()},
                {"uptime", gateway_server.get_uptime_seconds()},
                {"sessions", sessions.size()},
                {"version", "0.2.0"}
            });
        }
    );

    // --- GET /api/config ---
    server.add_raw_route("/api/config", "GET",
        [&config](const httplib::Request& req, httplib::Response& res) {
            std::string path = req.get_param_value("path");

            if (path.empty()) {
                json_ok(res, {
                    {"agent", {
                        {"model", config.agent.model},
                        {"maxIterations", config.agent.max_iterations},
                        {"temperature", config.agent.temperature}
                    }},
                    {"gateway", {
                        {"port", config.gateway.port},
                        {"bind", config.gateway.bind}
                    }}
                });
                return;
            }

            if (path == "gateway.port") { json_ok(res, config.gateway.port); return; }
            if (path == "gateway.bind") { json_ok(res, config.gateway.bind); return; }
            if (path == "agent.model") { json_ok(res, config.agent.model); return; }
            if (path == "agent.maxIterations") { json_ok(res, config.agent.max_iterations); return; }
            if (path == "agent.temperature") { json_ok(res, config.agent.temperature); return; }

            json_error(res, 400, "Unknown config path: " + path);
        }
    );

    // --- POST /api/agent/request ---
    server.add_raw_route("/api/agent/request", "POST",
        [session_manager, agent_loop, prompt_builder, logger]
        (const httplib::Request& req, httplib::Response& res) {
            try {
                auto params = nlohmann::json::parse(req.body);
                std::string session_key = params.value("sessionKey", "agent:main:main");
                std::string message = params.value("message", "");

                if (message.empty()) {
                    json_error(res, 400, "message is required");
                    return;
                }

                // Get or create session
                session_manager->get_or_create(session_key, "", "api");

                // Auto-generate display_name from first message
                auto sessions = session_manager->list_sessions();
                for (const auto& s : sessions) {
                    if (s.session_key == session_key && s.display_name == session_key) {
                        std::string truncated = message.substr(0, 50);
                        session_manager->update_display_name(session_key, truncated);
                        break;
                    }
                }

                // Append user message
                session_manager->append_message(session_key, "user", message);

                // Build system prompt
                std::string system_prompt = prompt_builder->build_full();

                // Load history
                auto history = session_manager->get_history(session_key, 50);

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
                auto new_messages = agent_loop->process_message(
                    message, llm_history, system_prompt);

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
                    session_manager->append_message(session_key, smsg);
                }

                json_ok(res, {
                    {"sessionKey", session_key},
                    {"response", final_response}
                });
            } catch (const nlohmann::json::exception& e) {
                json_error(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const std::exception& e) {
                json_error(res, 500, e.what());
            }
        }
    );

    // --- POST /api/agent/stream (SSE) ---
    // Server-Sent Events endpoint: streams agent events in real-time.
    // Request body: {"sessionKey": "...", "message": "..."}
    // Response: text/event-stream with events:
    //   event: agent.text_delta   data: {"text": "..."}
    //   event: agent.tool_use     data: {"id": "...", "name": "...", "input": {...}}
    //   event: agent.tool_result  data: {"tool_use_id": "...", "content": "..."}
    //   event: agent.message_end  data: {"content": "..."}
    //   event: done               data: {"sessionKey": "...", "response": "..."}
    server.add_raw_route("/api/agent/stream", "POST",
        [session_manager, agent_loop, prompt_builder, logger]
        (const httplib::Request& req, httplib::Response& res) {
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
            session_manager->get_or_create(session_key, "", "api");

            auto sessions = session_manager->list_sessions();
            for (const auto& s : sessions) {
                if (s.session_key == session_key && s.display_name == session_key) {
                    session_manager->update_display_name(
                        session_key, message.substr(0, 50));
                    break;
                }
            }

            session_manager->append_message(session_key, "user", message);
            std::string system_prompt = prompt_builder->build_full();

            auto history = session_manager->get_history(session_key, 50);
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
            std::thread worker(
                [state, session_manager, agent_loop,
                 message, llm_history = std::move(llm_history),
                 system_prompt, session_key, logger]() {
                    try {
                        auto new_messages = agent_loop->process_message_stream(
                            message, llm_history, system_prompt,
                            [&state](const quantclaw::AgentEvent& event) {
                                std::string sse = "event: " + event.type +
                                    "\ndata: " + event.data.dump() + "\n\n";
                                std::lock_guard<std::mutex> lock(state->mu);
                                state->chunks.push(std::move(sse));
                                state->cv.notify_one();
                            }
                        );

                        // Persist new messages to the session transcript
                        for (const auto& msg : new_messages) {
                            quantclaw::SessionMessage smsg;
                            smsg.role = msg.role;
                            smsg.content = msg.content;
                            session_manager->append_message(session_key, smsg);
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
                        nlohmann::json done_data = {
                            {"sessionKey", session_key},
                            {"response", final_response}
                        };
                        std::string done_sse =
                            "event: done\ndata: " + done_data.dump() + "\n\n";

                        std::lock_guard<std::mutex> lock(state->mu);
                        state->chunks.push(std::move(done_sse));
                        state->finished = true;
                        state->cv.notify_one();
                    } catch (const std::exception& e) {
                        nlohmann::json err = {{"error", e.what()}};
                        std::string err_sse =
                            "event: error\ndata: " + err.dump() + "\n\n";

                        std::lock_guard<std::mutex> lock(state->mu);
                        state->chunks.push(std::move(err_sse));
                        state->finished = true;
                        state->cv.notify_one();
                    }
                }
            );
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
                }
            );
        }
    );

    // --- POST /api/agent/stop ---
    server.add_raw_route("/api/agent/stop", "POST",
        [agent_loop](const httplib::Request&, httplib::Response& res) {
            agent_loop->stop();
            json_ok(res, {{"ok", true}});
        }
    );

    // --- GET /api/sessions ---
    server.add_raw_route("/api/sessions", "GET",
        [session_manager](const httplib::Request& req, httplib::Response& res) {
            int limit = 50;
            int offset = 0;
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); }
                catch (...) {}
            }
            if (req.has_param("offset")) {
                try { offset = std::stoi(req.get_param_value("offset")); }
                catch (...) {}
            }

            auto sessions = session_manager->list_sessions();
            nlohmann::json result = nlohmann::json::array();
            int end = std::min(offset + limit, static_cast<int>(sessions.size()));
            for (int i = offset; i < end; ++i) {
                result.push_back({
                    {"key", sessions[i].session_key},
                    {"id", sessions[i].session_id},
                    {"updatedAt", sessions[i].updated_at},
                    {"createdAt", sessions[i].created_at},
                    {"displayName", sessions[i].display_name},
                    {"channel", sessions[i].channel}
                });
            }
            json_ok(res, result);
        }
    );

    // --- GET /api/sessions/history ---
    server.add_raw_route("/api/sessions/history", "GET",
        [session_manager](const httplib::Request& req, httplib::Response& res) {
            std::string session_key = req.get_param_value("sessionKey");
            if (session_key.empty()) {
                json_error(res, 400, "sessionKey query parameter is required");
                return;
            }
            int limit = -1;
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); }
                catch (...) {}
            }

            auto history = session_manager->get_history(session_key, limit);
            nlohmann::json result = nlohmann::json::array();
            for (const auto& msg : history) {
                nlohmann::json entry;
                entry["role"] = msg.role;
                entry["timestamp"] = msg.timestamp;

                nlohmann::json content_arr = nlohmann::json::array();
                for (const auto& block : msg.content) {
                    content_arr.push_back(block.to_json());
                }
                entry["content"] = content_arr;

                if (msg.usage) {
                    entry["usage"] = msg.usage->to_json();
                }
                result.push_back(entry);
            }
            json_ok(res, result);
        }
    );

    // --- POST /api/sessions/delete ---
    server.add_raw_route("/api/sessions/delete", "POST",
        [session_manager](const httplib::Request& req, httplib::Response& res) {
            try {
                auto params = nlohmann::json::parse(req.body);
                std::string session_key = params.value("sessionKey", "");
                if (session_key.empty()) {
                    json_error(res, 400, "sessionKey is required");
                    return;
                }
                session_manager->delete_session(session_key);
                json_ok(res, {{"ok", true}});
            } catch (const nlohmann::json::exception& e) {
                json_error(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const std::exception& e) {
                json_error(res, 500, e.what());
            }
        }
    );

    // --- POST /api/sessions/reset ---
    server.add_raw_route("/api/sessions/reset", "POST",
        [session_manager](const httplib::Request& req, httplib::Response& res) {
            try {
                auto params = nlohmann::json::parse(req.body);
                std::string session_key = params.value("sessionKey", "");
                if (session_key.empty()) {
                    json_error(res, 400, "sessionKey is required");
                    return;
                }
                session_manager->reset_session(session_key);
                json_ok(res, {{"ok", true}});
            } catch (const nlohmann::json::exception& e) {
                json_error(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const std::exception& e) {
                json_error(res, 500, e.what());
            }
        }
    );

    // --- POST /api/config/reload ---
    if (reload_fn) {
        server.add_raw_route("/api/config/reload", "POST",
            [reload_fn, logger](const httplib::Request&, httplib::Response& res) {
                try {
                    reload_fn();
                    json_ok(res, {{"ok", true}});
                } catch (const std::exception& e) {
                    json_error(res, 500, e.what());
                }
            }
        );
    }

    // --- GET /api/channels ---
    server.add_raw_route("/api/channels", "GET",
        [](const httplib::Request&, httplib::Response& res) {
            json_ok(res, nlohmann::json::array({
                {{"name", "cli"}, {"type", "cli"}, {"status", "active"}}
            }));
        }
    );

    // --- POST /api/channel/message ---
    // Generic webhook endpoint for external integrations (e.g. Feishu, DingTalk, custom bots)
    // Accepts: {"channel": "discord", "senderId": "...", "channelId": "...", "message": "..."}
    // Returns: {"response": "...", "sessionKey": "..."}
    server.add_raw_route("/api/channel/message", "POST",
        [session_manager, agent_loop, prompt_builder, logger]
        (const httplib::Request& req, httplib::Response& res) {
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
                session_manager->get_or_create(session_key, "", channel);

                // Auto-generate display_name
                auto sessions = session_manager->list_sessions();
                for (const auto& s : sessions) {
                    if (s.session_key == session_key && s.display_name == session_key) {
                        session_manager->update_display_name(session_key,
                            channel + ": " + message.substr(0, 40));
                        break;
                    }
                }

                // Append user message
                session_manager->append_message(session_key, "user", message);

                // Build system prompt
                std::string system_prompt = prompt_builder->build_full();

                // Load history
                auto history = session_manager->get_history(session_key, 50);
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
                auto new_messages = agent_loop->process_message(
                    message, llm_history, system_prompt);

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
                    session_manager->append_message(session_key, smsg);
                }

                json_ok(res, {
                    {"sessionKey", session_key},
                    {"response", final_response}
                });
            } catch (const nlohmann::json::exception& e) {
                json_error(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const std::exception& e) {
                json_error(res, 500, e.what());
            }
        }
    );

    logger->info("Registered {} HTTP API routes", reload_fn ? 13 : 12);
}

} // namespace quantclaw::web
