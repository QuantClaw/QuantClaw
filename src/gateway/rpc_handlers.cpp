// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/gateway_server.hpp"
#include "quantclaw/gateway/protocol.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/core/skill_loader.hpp"
#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/security/exec_approval.hpp"
#include "quantclaw/core/session_compaction.hpp"
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
    std::function<void()> reload_fn,
    std::shared_ptr<quantclaw::ProviderRegistry> provider_registry,
    std::shared_ptr<quantclaw::SkillLoader> skill_loader,
    std::shared_ptr<quantclaw::CronScheduler> cron_scheduler,
    std::shared_ptr<quantclaw::ExecApprovalManager> exec_approval_mgr)
{
    // --- gateway.health ---
    server.RegisterHandler(methods::kGatewayHealth,
        [&server, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            return {
                {"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- gateway.status ---
    server.RegisterHandler(methods::kGatewayStatus,
        [&server, session_manager, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto sessions = session_manager->ListSessions();
            return {
                {"running", true},
                {"port", server.GetPort()},
                {"connections", server.GetConnectionCount()},
                {"uptime", server.GetUptimeSeconds()},
                {"sessions", sessions.size()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- config.get ---
    server.RegisterHandler(methods::kConfigGet,
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
    server.RegisterHandler(methods::kConfigSet,
        [logger, reload_fn](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
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

        // Remove the last message (the one we just appended) since process_message adds it
        if (!llm_history.empty()) {
            llm_history.pop_back();
        }

        // Send streaming events to the client
        std::string final_response;
        auto wrapped_callback = [&event_callback, &final_response](const quantclaw::AgentEvent& event) {
            event_callback(event);
            if (event.type == events::kMessageEnd && event.data.contains("content")) {
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

        return {session_key, final_response};
    };

    // --- agent.request ---
    server.RegisterHandler(methods::kAgentRequest,
        [execute_agent_request, &server, logger]
        (const nlohmann::json& params, ClientConnection& client) -> nlohmann::json {
            auto result = execute_agent_request(params, client,
                [&server, &client, logger](const quantclaw::AgentEvent& event) {
                    RpcEvent rpc_event;
                    rpc_event.event = event.type;
                    rpc_event.payload = event.data;
                    server.SendEventTo(client.connection_id, rpc_event);
                }
            );
            return {
                {"sessionKey", result.session_key},
                {"response", result.final_response}
            };
        }
    );

    // --- agent.stop ---
    server.RegisterHandler(methods::kAgentStop,
        [agent_loop, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            agent_loop->Stop();
            return {{"ok", true}};
        }
    );

    // --- sessions.list ---
    server.RegisterHandler(methods::kSessionsList,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            int limit = params.value("limit", 50);
            int offset = params.value("offset", 0);

            auto sessions = session_manager->ListSessions();

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
    server.RegisterHandler(methods::kSessionsHistory,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
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
        }
    );

    // --- sessions.delete ---
    server.RegisterHandler(methods::kSessionsDelete,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }
            session_manager->DeleteSession(session_key);
            return {{"ok", true}};
        }
    );

    // --- sessions.reset ---
    server.RegisterHandler(methods::kSessionsReset,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }
            session_manager->ResetSession(session_key);
            return {{"ok", true}};
        }
    );

    // --- channels.list ---
    server.RegisterHandler(methods::kChannelsList,
        [&config, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            nlohmann::json result = nlohmann::json::array();
            // CLI channel is always present
            result.push_back({{"id", "cli"}, {"type", "cli"}, {"enabled", true}, {"status", "active"}});
            // Add configured channels
            for (const auto& [id, ch] : config.channels) {
                nlohmann::json entry;
                entry["id"] = id;
                entry["type"] = id;  // type is typically same as id (discord, telegram, etc.)
                entry["enabled"] = ch.enabled;
                entry["status"] = ch.enabled ? "active" : "disabled";
                result.push_back(entry);
            }
            return result;
        }
    );

    // --- channels.status ---
    server.RegisterHandler(methods::kChannelsStatus,
        [&config, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string id = params.value("id", "");
            if (!id.empty()) {
                // Status of a specific channel
                if (id == "cli") {
                    return {{"id", "cli"}, {"type", "cli"}, {"enabled", true}, {"status", "active"}};
                }
                auto it = config.channels.find(id);
                if (it == config.channels.end()) {
                    throw std::runtime_error("Channel not found: " + id);
                }
                nlohmann::json r;
                r["id"] = id;
                r["type"] = id;
                r["enabled"] = it->second.enabled;
                r["status"] = it->second.enabled ? "active" : "disabled";
                return r;
            }
            // Status of all channels
            nlohmann::json result;
            result["total"] = static_cast<int>(config.channels.size()) + 1;
            int active = 1;  // CLI always active
            for (const auto& [cid, ch] : config.channels) {
                if (ch.enabled) active++;
            }
            result["active"] = active;
            return result;
        }
    );

    // --- chain.execute ---
    server.RegisterHandler(methods::kChainExecute,
        [tool_registry, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            auto chain_def = quantclaw::ToolChainExecutor::ParseChain(params);
            quantclaw::ToolExecutorFn executor = [tool_registry](const std::string& name, const nlohmann::json& args) {
                return tool_registry->ExecuteTool(name, args);
            };
            quantclaw::ToolChainExecutor chain_executor(executor, logger);
            auto result = chain_executor.Execute(chain_def);
            return quantclaw::ToolChainExecutor::ResultToJson(result);
        }
    );

    // --- config.reload ---
    if (reload_fn) {
        server.RegisterHandler(methods::kConfigReload,
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
    server.RegisterHandler(methods::kOcChatSend,
        [execute_agent_request, &server, logger]
        (const nlohmann::json& params, ClientConnection& client) -> nlohmann::json {
            auto result = execute_agent_request(params, client,
                [&server, &client, logger](const quantclaw::AgentEvent& event) {
                    RpcEvent rpc_event;

                    if (event.type == events::kTextDelta) {
                        // agent.text_delta → event "agent" {stream:"assistant", data:{text}}
                        rpc_event.event = events::kOcAgent;
                        rpc_event.payload = {
                            {"stream", "assistant"},
                            {"data", {{"text", event.data.value("text", "")}}}
                        };
                    } else if (event.type == events::kToolUse) {
                        // agent.tool_use → event "agent" {stream:"tool", data:{id,name,input}}
                        rpc_event.event = events::kOcAgent;
                        rpc_event.payload = {
                            {"stream", "tool"},
                            {"data", {
                                {"id", event.data.value("id", "")},
                                {"name", event.data.value("name", "")},
                                {"input", event.data.value("input", nlohmann::json::object())}
                            }}
                        };
                    } else if (event.type == events::kToolResult) {
                        // agent.tool_result → event "agent" {stream:"tool_result", data:{tool_use_id,content}}
                        rpc_event.event = events::kOcAgent;
                        rpc_event.payload = {
                            {"stream", "tool_result"},
                            {"data", {
                                {"tool_use_id", event.data.value("tool_use_id", "")},
                                {"content", event.data.value("content", "")}
                            }}
                        };
                    } else if (event.type == events::kMessageEnd) {
                        // agent.message_end → event "chat" {state:"final", content}
                        rpc_event.event = events::kOcChat;
                        rpc_event.payload = {
                            {"state", "final"},
                            {"content", event.data.value("content", "")}
                        };
                    } else {
                        // Pass through any other events as-is
                        rpc_event.event = event.type;
                        rpc_event.payload = event.data;
                    }

                    server.SendEventTo(client.connection_id, rpc_event);
                }
            );
            return {
                {"sessionKey", result.session_key},
                {"response", result.final_response}
            };
        }
    );

    // --- chat.history (alias for sessions.history) ---
    server.RegisterHandler(methods::kOcChatHistory,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
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
        }
    );

    // --- chat.abort (alias for agent.stop) ---
    server.RegisterHandler(methods::kOcChatAbort,
        [agent_loop, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            agent_loop->Stop();
            return {{"ok", true}};
        }
    );

    // --- health (alias for gateway.health) ---
    server.RegisterHandler(methods::kOcHealth,
        [&server, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            return {
                {"status", "ok"},
                {"uptime", server.GetUptimeSeconds()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- status (alias for gateway.status) ---
    server.RegisterHandler(methods::kOcStatus,
        [&server, session_manager, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto sessions = session_manager->ListSessions();
            return {
                {"running", true},
                {"port", server.GetPort()},
                {"connections", server.GetConnectionCount()},
                {"uptime", server.GetUptimeSeconds()},
                {"sessions", sessions.size()},
                {"version", "0.2.0"}
            };
        }
    );

    // --- models.list ---
    server.RegisterHandler(methods::kOcModelsList,
        [&config, provider_registry, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            nlohmann::json models = nlohmann::json::array();

            // Active model
            models.push_back({
                {"id", config.agent.model},
                {"provider", "default"},
                {"active", true}
            });

            // List models from registered providers
            if (provider_registry) {
                for (const auto& pid : provider_registry->ProviderIds()) {
                    auto p = provider_registry->GetProvider(pid);
                    if (p) {
                        for (const auto& m : p->GetSupportedModels()) {
                            // Skip duplicate of active model
                            if (m == config.agent.model && pid == "default") continue;
                            models.push_back({
                                {"id", m},
                                {"provider", pid},
                                {"active", m == config.agent.model}
                            });
                        }
                    }
                }
            }

            return models;
        }
    );

    // --- tools.catalog ---
    server.RegisterHandler(methods::kOcToolsCatalog,
        [tool_registry, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
            auto schemas = tool_registry->GetToolSchemas();
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
    server.RegisterHandler(methods::kOcSessionsPreview,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
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
        }
    );

    // --- sessions.patch ---
    server.RegisterHandler(methods::kSessionsPatch,
        [session_manager, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string session_key = params.value("sessionKey", "");
            if (session_key.empty()) {
                throw std::runtime_error("sessionKey is required");
            }

            if (params.contains("displayName")) {
                session_manager->UpdateDisplayName(session_key, params["displayName"]);
            }

            return {{"ok", true}};
        }
    );

    // --- sessions.compact ---
    server.RegisterHandler(methods::kSessionsCompact,
        [session_manager, agent_loop, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
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

            return {
                {"compacted", true},
                {"originalCount", static_cast<int>(history.size())},
                {"newCount", static_cast<int>(compacted.size())}
            };
        }
    );

    // --- skills.status ---
    if (skill_loader) {
        server.RegisterHandler(methods::kSkillsStatus,
            [skill_loader, &config, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
                std::string home_str;
                const char* home = std::getenv("HOME");
                if (home) home_str = home;
                else home_str = "/tmp";
                auto workspace_path = std::filesystem::path(home_str) /
                                      ".quantclaw/agents/main/workspace";

                auto skills = skill_loader->LoadSkills(config.skills, workspace_path);

                nlohmann::json result = nlohmann::json::array();
                for (const auto& skill : skills) {
                    nlohmann::json s;
                    s["name"] = skill.name;
                    s["description"] = skill.description;
                    s["emoji"] = skill.emoji;
                    s["rootDir"] = skill.root_dir.string();
                    s["gated"] = !skill_loader->CheckSkillGating(skill);
                    result.push_back(s);
                }
                return result;
            }
        );

        // --- skills.install ---
        server.RegisterHandler(methods::kSkillsInstall,
            [skill_loader, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
                std::string name = params.value("name", "");
                if (name.empty()) {
                    throw std::runtime_error("skill name is required");
                }

                quantclaw::SkillMetadata meta;
                meta.name = name;
                meta.root_dir = params.value("rootDir", "");

                bool ok = skill_loader->InstallSkill(meta);
                return {{"ok", ok}, {"name", name}};
            }
        );
    }

    // --- cron.update ---
    if (cron_scheduler) {
        server.RegisterHandler(methods::kCronUpdate,
            [cron_scheduler, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
                std::string id = params.value("id", "");
                if (id.empty()) {
                    throw std::runtime_error("cron job id is required");
                }

                auto jobs = cron_scheduler->ListJobs();
                for (const auto& job : jobs) {
                    if (job.id == id || job.id.substr(0, id.size()) == id) {
                        // Found the job — remove and re-add with updated fields
                        std::string name = params.value("name", job.name);
                        std::string schedule = params.value("schedule", job.schedule);
                        std::string message = params.value("message", job.message);

                        cron_scheduler->RemoveJob(job.id);
                        auto new_id = cron_scheduler->AddJob(name, schedule, message, job.session_key);
                        return {{"ok", true}, {"id", new_id}};
                    }
                }

                throw std::runtime_error("cron job not found: " + id);
            }
        );

        // --- cron.run ---
        server.RegisterHandler(methods::kCronRun,
            [cron_scheduler, agent_loop, session_manager, prompt_builder, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
                std::string id = params.value("id", "");
                if (id.empty()) {
                    throw std::runtime_error("cron job id is required");
                }

                auto jobs = cron_scheduler->ListJobs();
                for (const auto& job : jobs) {
                    if (job.id == id || job.id.substr(0, id.size()) == id) {
                        // Execute the cron job's message as an agent request
                        auto session = session_manager->GetOrCreate(job.session_key, job.name, "cron");
                        auto history_msgs = session_manager->GetHistory(job.session_key);

                        std::vector<quantclaw::Message> history;
                        for (const auto& m : history_msgs) {
                            quantclaw::Message msg;
                            msg.role = m.role;
                            msg.content = m.content;
                            history.push_back(msg);
                        }

                        auto system_prompt = prompt_builder->BuildFull(job.session_key);
                        auto new_msgs = agent_loop->ProcessMessage(job.message, history, system_prompt);

                        // Store messages
                        for (const auto& msg : new_msgs) {
                            quantclaw::SessionMessage sm;
                            sm.role = msg.role;
                            sm.content = msg.content;
                            session_manager->AppendMessage(job.session_key, sm);
                        }

                        nlohmann::json r; r["ok"] = true; r["jobId"] = job.id; r["messagesGenerated"] = static_cast<int>(new_msgs.size()); return r;
                    }
                }

                throw std::runtime_error("cron job not found: " + id);
            }
        );

        // --- cron.runs ---
        server.RegisterHandler(methods::kCronRuns,
            [cron_scheduler, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
                std::string id = params.value("id", "");
                auto jobs = cron_scheduler->ListJobs();

                nlohmann::json result = nlohmann::json::array();
                for (const auto& job : jobs) {
                    if (!id.empty() && job.id.substr(0, id.size()) != id && job.id != id) {
                        continue;
                    }
                    nlohmann::json entry;
                    entry["id"] = job.id;
                    entry["name"] = job.name;
                    entry["schedule"] = job.schedule;
                    entry["enabled"] = job.enabled;
                    auto last_run_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        job.last_run.time_since_epoch()).count();
                    auto next_run_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                        job.next_run.time_since_epoch()).count();
                    entry["lastRun"] = last_run_epoch > 0 ? last_run_epoch : 0;
                    entry["nextRun"] = next_run_epoch > 0 ? next_run_epoch : 0;
                    result.push_back(entry);
                }
                return result;
            }
        );
    }

    // --- exec.approval.request ---
    if (exec_approval_mgr) {
        server.RegisterHandler(methods::kExecApprovalReq,
            [exec_approval_mgr, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
                std::string command = params.value("command", "");
                if (command.empty()) {
                    throw std::runtime_error("command is required");
                }

                std::string cwd = params.value("cwd", "");
                std::string agent_id = params.value("agentId", "");
                std::string session_key = params.value("sessionKey", "");

                auto decision = exec_approval_mgr->RequestApproval(
                    command, cwd, agent_id, session_key);

                std::string decision_str;
                switch (decision) {
                    case quantclaw::ApprovalDecision::kApproved: decision_str = "approved"; break;
                    case quantclaw::ApprovalDecision::kDenied: decision_str = "denied"; break;
                    case quantclaw::ApprovalDecision::kPending: decision_str = "pending"; break;
                    default: decision_str = "timeout"; break;
                }

                return {{"decision", decision_str}};
            }
        );

        // --- exec.approvals.get ---
        server.RegisterHandler(methods::kExecApprovals,
            [exec_approval_mgr, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
                const auto& cfg = exec_approval_mgr->GetConfig();

                std::string mode_str;
                switch (cfg.ask) {
                    case quantclaw::AskMode::kOff: mode_str = "off"; break;
                    case quantclaw::AskMode::kOnMiss: mode_str = "on-miss"; break;
                    case quantclaw::AskMode::kAlways: mode_str = "always"; break;
                }

                nlohmann::json patterns = nlohmann::json::array();
                for (const auto& p : cfg.allowlist) {
                    patterns.push_back(p);
                }

                auto pending = exec_approval_mgr->PendingRequests();
                nlohmann::json pending_json = nlohmann::json::array();
                for (const auto& req : pending) {
                    pending_json.push_back({
                        {"id", req.id},
                        {"command", req.command},
                        {"cwd", req.cwd},
                        {"agentId", req.agent_id},
                        {"sessionKey", req.session_key}
                    });
                }

                return {
                    {"mode", mode_str},
                    {"allowlist", patterns},
                    {"pending", pending_json}
                };
            }
        );
    }

    // --- models.set ---
    server.RegisterHandler(methods::kModelsSet,
        [agent_loop, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
            std::string model = params.value("model", "");
            if (model.empty()) {
                throw std::runtime_error("model is required");
            }
            agent_loop->SetModel(model);
            return {{"ok", true}, {"model", model}};
        }
    );

    int handler_count = 22;  // base handlers
    if (reload_fn) handler_count++;
    if (skill_loader) handler_count += 2;
    if (cron_scheduler) handler_count += 3;
    if (exec_approval_mgr) handler_count += 2;
    logger->info("Registered {} RPC handlers", handler_count);
}

} // namespace quantclaw::gateway
