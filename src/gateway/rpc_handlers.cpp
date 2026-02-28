#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/config.hpp"
#include <chrono>
#include <functional>
#include <sstream>

namespace quantclaw::gateway {

// Helper to register all RPC handlers on a GatewayServer
void register_rpc_handlers(
    GatewayServer& server,
    std::shared_ptr<quantclaw::SessionManager> session_manager,
    std::shared_ptr<quantclaw::AgentLoop> agent_loop,
    std::shared_ptr<quantclaw::PromptBuilder> prompt_builder,
    std::shared_ptr<quantclaw::ToolRegistry> tool_registry,
    const quantclaw::QuantClawConfig& config,
    std::shared_ptr<spdlog::logger> logger,
    std::function<void()> reload_fn)
{
    // --- gateway.health ---
    server.register_handler(methods::GATEWAY_HEALTH,
        [&server, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            return {
                {"status", "ok"},
                {"uptime", server.get_uptime_seconds()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- gateway.status ---
    server.register_handler(methods::GATEWAY_STATUS,
        [&server, session_manager, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto sessions = session_manager->list_sessions();
            return {
                {"running", true},
                {"port", server.get_port()},
                {"connections", server.get_connection_count()},
                {"uptime", server.get_uptime_seconds()},
                {"sessions", sessions.size()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- config.get ---
    server.register_handler(methods::CONFIG_GET,
        [&config, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string path = params.value("path", "");

            if (path.empty()) {
                // Return full config summary
                return {
                    {"agent", {
                        {"model", config.agent.model},
                        {"maxIterations", config.agent.max_iterations},
                        {"temperature", config.agent.temperature}
                    }},
                    {"gateway", {
                        {"port", config.gateway.port},
                        {"bind", config.gateway.bind}
                    }}
                };
            }

            // Dot-path lookup
            if (path == "gateway.port") return config.gateway.port;
            if (path == "gateway.bind") return config.gateway.bind;
            if (path == "agent.model") return config.agent.model;
            if (path == "agent.maxIterations") return config.agent.max_iterations;
            if (path == "agent.temperature") return config.agent.temperature;

            throw std::runtime_error("Unknown config path: " + path);
        }
    );

    // --- config.set ---
    server.register_handler(methods::CONFIG_SET,
        [logger, reload_fn](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string path = params.value("path", "");
            if (path.empty()) {
                throw std::runtime_error("path is required");
            }
            if (!params.contains("value")) {
                throw std::runtime_error("value is required");
            }

            auto config_file = QuantClawConfig::default_config_path();
            QuantClawConfig::set_value(config_file, path, params["value"]);

            // Trigger hot-reload so the running server picks up the change
            if (reload_fn) {
                reload_fn();
            }

            return {{"ok", true}, {"path", path}};
        }
    );

    // --- Shared agent request helper ---
    // Extracted so both agent.request and chat.send can reuse the core logic
    struct AgentRequestResult {
        std::string session_key;
        std::string final_response;
    };

    auto execute_agent_request = [session_manager, agent_loop, prompt_builder, logger, &server](
        const nlohmann::json& params, ClientConnection& client,
        quantclaw::AgentEventCallback event_callback) -> AgentRequestResult
    {
        std::string session_key = params.value("sessionKey", "agent:main:main");
        std::string message = params.value("message", "");

        if (message.empty()) {
            throw std::runtime_error("message is required");
        }

        // Get or create session
        session_manager->get_or_create(session_key, "", "cli");

        // Auto-generate display_name from first user message
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

        // Convert SessionMessages to LLM Messages (lossless copy)
        std::vector<quantclaw::Message> llm_history;
        for (const auto& smsg : history) {
            quantclaw::Message m;
            m.role = smsg.role;
            m.content = smsg.content;
            llm_history.push_back(m);
        }

        // Remove the last message (the one we just appended) since process_message adds it
        if (!llm_history.empty()) {
            llm_history.pop_back();
        }

        // Send streaming events to the client
        std::string final_response;
        auto wrapped_callback = [&event_callback, &final_response](const quantclaw::AgentEvent& event) {
            event_callback(event);
            if (event.type == events::MESSAGE_END && event.data.contains("content")) {
                final_response = event.data["content"].get<std::string>();
            }
        };

        auto new_messages = agent_loop->process_message_stream(
            message, llm_history, system_prompt, wrapped_callback);

        // Persist all new messages (assistant + tool_result) to session transcript
        for (const auto& msg : new_messages) {
            quantclaw::SessionMessage smsg;
            smsg.role = msg.role;
            smsg.content = msg.content;
            session_manager->append_message(session_key, smsg);
        }

        return {session_key, final_response};
    };

    // --- agent.request ---
    server.register_handler(methods::AGENT_REQUEST,
        [execute_agent_request, &server, logger]
        (const nlohmann::json& params, ClientConnection& client) -> nlohmann::json {
            auto result = execute_agent_request(params, client,
                [&server, &client, logger](const quantclaw::AgentEvent& event) {
                    RpcEvent rpc_event;
                    rpc_event.event = event.type;
                    rpc_event.payload = event.data;
                    server.send_event_to(client.connection_id, rpc_event);
                }
            );
            return {
                {"sessionKey", result.session_key},
                {"response", result.final_response}
            };
        }
    );

    // --- agent.stop ---
    server.register_handler(methods::AGENT_STOP,
        [agent_loop, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            agent_loop->stop();
            return {{"ok", true}};
        }
    );

    // --- sessions.list ---
    server.register_handler(methods::SESSIONS_LIST,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            int limit = params.value("limit", 50);
            int offset = params.value("offset", 0);

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
            return result;
        }
    );

    // --- sessions.history ---
    server.register_handler(methods::SESSIONS_HISTORY,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            int limit = params.value("limit", -1);

            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }

            auto history = session_manager->get_history(session_key, limit);

            nlohmann::json result = nlohmann::json::array();
            for (const auto& msg : history) {
                nlohmann::json entry;
                entry["role"] = msg.role;
                entry["timestamp"] = msg.timestamp;

                // Return full ContentBlock array
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
            return result;
        }
    );

    // --- sessions.delete ---
    server.register_handler(methods::SESSIONS_DELETE,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }
            session_manager->delete_session(session_key);
            return {{"ok", true}};
        }
    );

    // --- sessions.reset ---
    server.register_handler(methods::SESSIONS_RESET,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }
            session_manager->reset_session(session_key);
            return {{"ok", true}};
        }
    );

    // --- channels.list ---
    server.register_handler(methods::CHANNELS_LIST,
        [logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            // Phase 1: only CLI channel
            return nlohmann::json::array({
                {{"name", "cli"}, {"type", "cli"}, {"status", "active"}}
            });
        }
    );

    // --- chain.execute ---
    server.register_handler(methods::CHAIN_EXECUTE,
        [tool_registry, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            auto chain_def = quantclaw::ToolChainExecutor::parse_chain(params);
            quantclaw::ToolExecutorFn executor = [tool_registry](const std::string& name, const nlohmann::json& args) {
                return tool_registry->execute_tool(name, args);
            };
            quantclaw::ToolChainExecutor chain_executor(executor, logger);
            auto result = chain_executor.execute(chain_def);
            return quantclaw::ToolChainExecutor::result_to_json(result);
        }
    );

    // --- config.reload ---
    if (reload_fn) {
        server.register_handler(methods::CONFIG_RELOAD,
            [reload_fn, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
                reload_fn();
                return {{"ok", true}};
            }
        );
    }

    // ================================================================
    // OpenClaw-compatible RPC handlers (protocol shim)
    // ================================================================

    // --- chat.send (OpenClaw) ---
    // Translates QuantClaw agent events to OpenClaw format
    server.register_handler(methods::OC_CHAT_SEND,
        [execute_agent_request, &server, logger]
        (const nlohmann::json& params, ClientConnection& client) -> nlohmann::json {
            auto result = execute_agent_request(params, client,
                [&server, &client, logger](const quantclaw::AgentEvent& event) {
                    RpcEvent rpc_event;

                    if (event.type == events::TEXT_DELTA) {
                        // agent.text_delta → event "agent" {stream:"assistant", data:{text}}
                        rpc_event.event = events::OC_AGENT;
                        rpc_event.payload = {
                            {"stream", "assistant"},
                            {"data", {{"text", event.data.value("text", "")}}}
                        };
                    } else if (event.type == events::TOOL_USE) {
                        // agent.tool_use → event "agent" {stream:"tool", data:{id,name,input}}
                        rpc_event.event = events::OC_AGENT;
                        rpc_event.payload = {
                            {"stream", "tool"},
                            {"data", {
                                {"id", event.data.value("id", "")},
                                {"name", event.data.value("name", "")},
                                {"input", event.data.value("input", nlohmann::json::object())}
                            }}
                        };
                    } else if (event.type == events::TOOL_RESULT) {
                        // agent.tool_result → event "agent" {stream:"tool_result", data:{tool_use_id,content}}
                        rpc_event.event = events::OC_AGENT;
                        rpc_event.payload = {
                            {"stream", "tool_result"},
                            {"data", {
                                {"tool_use_id", event.data.value("tool_use_id", "")},
                                {"content", event.data.value("content", "")}
                            }}
                        };
                    } else if (event.type == events::MESSAGE_END) {
                        // agent.message_end → event "chat" {state:"final", content}
                        rpc_event.event = events::OC_CHAT;
                        rpc_event.payload = {
                            {"state", "final"},
                            {"content", event.data.value("content", "")}
                        };
                    } else {
                        // Pass through any other events as-is
                        rpc_event.event = event.type;
                        rpc_event.payload = event.data;
                    }

                    server.send_event_to(client.connection_id, rpc_event);
                }
            );
            return {
                {"sessionKey", result.session_key},
                {"response", result.final_response}
            };
        }
    );

    // --- chat.history (alias for sessions.history) ---
    server.register_handler(methods::OC_CHAT_HISTORY,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            int limit = params.value("limit", -1);

            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
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
            return result;
        }
    );

    // --- chat.abort (alias for agent.stop) ---
    server.register_handler(methods::OC_CHAT_ABORT,
        [agent_loop, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            agent_loop->stop();
            return {{"ok", true}};
        }
    );

    // --- health (alias for gateway.health) ---
    server.register_handler(methods::OC_HEALTH,
        [&server, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            return {
                {"status", "ok"},
                {"uptime", server.get_uptime_seconds()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- status (alias for gateway.status) ---
    server.register_handler(methods::OC_STATUS,
        [&server, session_manager, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto sessions = session_manager->list_sessions();
            return {
                {"running", true},
                {"port", server.get_port()},
                {"connections", server.get_connection_count()},
                {"uptime", server.get_uptime_seconds()},
                {"sessions", sessions.size()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- models.list (stub) ---
    server.register_handler(methods::OC_MODELS_LIST,
        [&config, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            return nlohmann::json::array({
                {{"id", config.agent.model}, {"provider", "default"}, {"active", true}}
            });
        }
    );

    // --- tools.catalog ---
    server.register_handler(methods::OC_TOOLS_CATALOG,
        [tool_registry, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto schemas = tool_registry->get_tool_schemas();
            nlohmann::json result = nlohmann::json::array();
            for (const auto& schema : schemas) {
                result.push_back({
                    {"name", schema.name},
                    {"description", schema.description},
                    {"parameters", schema.parameters}
                });
            }
            return result;
        }
    );

    // --- sessions.preview ---
    server.register_handler(methods::OC_SESSIONS_PREVIEW,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }

            auto history = session_manager->get_history(session_key, 1);
            if (history.empty()) {
                return nlohmann::json::object();
            }

            const auto& msg = history.back();
            nlohmann::json entry;
            entry["role"] = msg.role;
            entry["timestamp"] = msg.timestamp;

            nlohmann::json content_arr = nlohmann::json::array();
            for (const auto& block : msg.content) {
                content_arr.push_back(block.to_json());
            }
            entry["content"] = content_arr;

            return entry;
        }
    );

    int handler_count = reload_fn ? 21 : 20;
    logger->info("Registered {} RPC handlers", handler_count);
}

} // namespace quantclaw::gateway
