// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/tools/tool_registry.hpp"
#include "quantclaw/security/sandbox.hpp"
#include "quantclaw/security/tool_permissions.hpp"
#include "quantclaw/security/exec_approval.hpp"
#include "quantclaw/core/subagent.hpp"
#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/core/memory_search.hpp"
#include "quantclaw/session/session_manager.hpp"
#include "quantclaw/tools/tool_chain.hpp"
#include "quantclaw/mcp/mcp_tool_manager.hpp"
#include "quantclaw/platform/process.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <random>
#include <regex>
#include <httplib.h>
#include <spdlog/spdlog.h>

namespace quantclaw {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string url_encode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            out << '%' << std::hex << std::uppercase
                << ((c >> 4) & 0xF) << (c & 0xF);
        }
    }
    return out.str();
}

// Strip HTML tags and decode basic entities → plain text
static std::string html_to_text(const std::string& html) {
    // Remove <script> and <style> blocks
    std::string text = std::regex_replace(html,
        std::regex("<(script|style)[^>]*>[\\s\\S]*?</(script|style)>",
                   std::regex::icase), " ");
    // Remove all remaining tags
    text = std::regex_replace(text, std::regex("<[^>]+>"), " ");
    // Decode basic entities
    auto replace_all = [](std::string s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    };
    text = replace_all(text, "&amp;",  "&");
    text = replace_all(text, "&lt;",   "<");
    text = replace_all(text, "&gt;",   ">");
    text = replace_all(text, "&quot;", "\"");
    text = replace_all(text, "&#39;",  "'");
    text = replace_all(text, "&nbsp;", " ");
    // Collapse whitespace
    text = std::regex_replace(text, std::regex("[ \t]+"), " ");
    text = std::regex_replace(text, std::regex("\n{3,}"), "\n\n");
    return text;
}

static std::string generate_id(const std::string& prefix = "bg") {
    thread_local static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist;
    std::ostringstream ss;
    ss << prefix << "_" << std::hex << dist(rng);
    return ss.str();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ToolRegistry::ToolRegistry(std::shared_ptr<spdlog::logger> logger)
    : logger_(logger) {
    logger_->info("ToolRegistry initialized");
}

// ---------------------------------------------------------------------------
// register_tool helper (deduplicates schema on re-register)
// ---------------------------------------------------------------------------

void ToolRegistry::register_tool(const std::string& name,
                                  const std::string& description,
                                  nlohmann::json params_schema,
                                  std::function<std::string(const nlohmann::json&)> handler) {
    tools_[name] = std::move(handler);
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [&name](const ToolSchema& s) { return s.name == name; }),
        tool_schemas_.end());
    tool_schemas_.push_back({name, description, std::move(params_schema)});
}

// ---------------------------------------------------------------------------
// RegisterBuiltinTools
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterBuiltinTools() {
    // ---- read ----
    register_tool("read", "Read the contents of a file",
        nlohmann::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"Path to the file to read"}},"required":["path"]})"),
        [this](const nlohmann::json& p) { return read_file_tool(p); });

    // ---- write ----
    register_tool("write", "Write content to a file",
        nlohmann::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"Path to write"},"content":{"type":"string","description":"Content to write"}},"required":["path","content"]})"),
        [this](const nlohmann::json& p) { return write_file_tool(p); });

    // ---- edit ----
    register_tool("edit", "Edit a file by replacing exact text",
        nlohmann::json::parse(R"({"type":"object","properties":{"path":{"type":"string"},"oldText":{"type":"string","description":"Exact text to replace"},"newText":{"type":"string","description":"Replacement text"}},"required":["path","oldText","newText"]})"),
        [this](const nlohmann::json& p) { return edit_file_tool(p); });

    // ---- exec ----
    register_tool("exec", "Execute a shell command and return its output",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"},"workdir":{"type":"string","description":"Working directory (optional)"},"timeout":{"type":"integer","description":"Timeout in seconds (default 30)"}},"required":["command"]})JSON"),
        [this](const nlohmann::json& p) { return exec_tool(p); });

    // ---- bash (OpenClaw alias for exec) ----
    register_tool("bash", "Execute a shell command (alias for exec)",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"},"timeout":{"type":"integer","description":"Timeout in seconds (default 30)"}},"required":["command"]})JSON"),
        [this](const nlohmann::json& p) { return exec_tool(p); });

    // ---- apply_patch ----
    register_tool("apply_patch",
        "Apply a multi-file patch in *** Begin Patch / *** End Patch format. "
        "Supports: *** Add File, *** Update File (with unified diff hunks), *** Delete File.",
        nlohmann::json::parse(R"({"type":"object","properties":{"patch":{"type":"string","description":"Patch text in *** Begin Patch ... *** End Patch format"}},"required":["patch"]})"),
        [this](const nlohmann::json& p) { return apply_patch_tool(p); });

    // ---- process ----
    register_tool("process",
        "Manage long-running background shell sessions. "
        "Actions: start, list, log, poll, write, send-keys, kill, clear, remove.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"action":{"type":"string","enum":["start","list","log","poll","write","send-keys","kill","clear","remove"],"description":"Action to perform"},"command":{"type":"string","description":"Shell command (required for start)"},"id":{"type":"string","description":"Session ID (required for all actions except start/list)"},"input":{"type":"string","description":"Text to write to stdin (for write/send-keys)"},"timeout":{"type":"integer","description":"Max wait ms for poll (default 5000)"}},"required":["action"]})JSON"),
        [this](const nlohmann::json& p) { return process_tool(p); });

    // ---- message ----
    register_tool("message", "Send a message to a channel",
        nlohmann::json::parse(R"({"type":"object","properties":{"channel":{"type":"string","description":"Channel to send to"},"message":{"type":"string","description":"Message content"},"action":{"type":"string","description":"Action: send (default), reply, react, pin, edit, delete"}},"required":["channel","message"]})"),
        [this](const nlohmann::json& p) { return message_tool(p); });

    // ---- web_search ----
    register_tool("web_search",
        "Search the web. Cascade: Brave (BRAVE_API_KEY), Tavily (TAVILY_API_KEY), "
        "Perplexity (PERPLEXITY_API_KEY), DuckDuckGo (free, no key), Grok (XAI_API_KEY). "
        "Returns titles, URLs, and snippets.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"query":{"type":"string","description":"Search query"},"count":{"type":"integer","description":"Number of results (1-10, default 5)"},"freshness":{"type":"string","description":"Time filter: day, week, month, year"}},"required":["query"]})JSON"),
        [this](const nlohmann::json& p) { return web_search_tool(p); });

    // ---- web_fetch ----
    register_tool("web_fetch",
        "Fetch a URL and return its content as plain text or markdown.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"url":{"type":"string","description":"URL to fetch"},"maxChars":{"type":"integer","description":"Max characters to return (default 50000)"}},"required":["url"]})JSON"),
        [this](const nlohmann::json& p) { return web_fetch_tool(p); });

    // ---- memory_search ----
    register_tool("memory_search",
        "Search agent memory files (MEMORY.md and workspace docs) using BM25 full-text search.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"query":{"type":"string","description":"Search query"},"maxResults":{"type":"integer","description":"Max results to return (default 10)"}},"required":["query"]})JSON"),
        [this](const nlohmann::json& p) { return memory_search_tool(p); });

    // ---- memory_get ----
    register_tool("memory_get",
        "Read a specific file from the agent workspace (MEMORY.md, notes, etc.).",
        nlohmann::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"Relative path within the workspace, e.g. MEMORY.md or memory/notes.md"}},"required":["path"]})"),
        [this](const nlohmann::json& p) { return memory_get_tool(p); });

    logger_->info("Registered {} built-in tools", tools_.size());
}

// ---------------------------------------------------------------------------
// RegisterChainTool
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterChainTool() {
    tools_["chain"] = [this](const nlohmann::json& params) -> std::string {
        auto chain_def = ToolChainExecutor::ParseChain(params);
        ToolExecutorFn executor = [this](const std::string& name, const nlohmann::json& args) {
            return ExecuteTool(name, args);
        };
        ToolChainExecutor chain_executor(executor, logger_);
        auto result = chain_executor.Execute(chain_def);
        return ToolChainExecutor::ResultToJson(result).dump();
    };

    nlohmann::json chain_params;
    chain_params["type"] = "object";
    chain_params["properties"] = {
        {"name",   {{"type", "string"}, {"description", "Name of the chain"}}},
        {"steps",  {
            {"type", "array"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"tool",      {{"type", "string"}, {"description", "Tool name"}}},
                    {"arguments", {{"type", "object"}, {"description", "Args, may use {{prev.result}} or {{steps[N].result}}"}}}
                }},
                {"required", {"tool"}}
            }},
            {"description", "Ordered tool invocations"}
        }},
        {"error_policy", {{"type", "string"}, {"enum", {"stop_on_error", "continue_on_error", "retry"}}}},
        {"max_retries",  {{"type", "integer"}}}
    };
    chain_params["required"] = {"steps"};

    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "chain"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({"chain",
        "Execute a pipeline of tools in sequence. Each step can reference previous results via "
        "{{prev.result}} or {{steps[N].result}} templates.",
        chain_params});

    logger_->info("Registered chain tool");
}

// ---------------------------------------------------------------------------
// SetPermissionChecker / SetMcpToolManager / SetApprovalManager
// ---------------------------------------------------------------------------

void ToolRegistry::SetPermissionChecker(std::shared_ptr<ToolPermissionChecker> checker) {
    permission_checker_ = std::move(checker);
}

void ToolRegistry::SetMcpToolManager(std::shared_ptr<mcp::MCPToolManager> manager) {
    mcp_tool_manager_ = std::move(manager);
}

void ToolRegistry::SetApprovalManager(std::shared_ptr<ExecApprovalManager> manager) {
    approval_manager_ = std::move(manager);
}

// ---------------------------------------------------------------------------
// SetSubagentManager — registers spawn_subagent tool
// ---------------------------------------------------------------------------

void ToolRegistry::SetSubagentManager(SubagentManager* manager,
                                       const std::string& session_key) {
    subagent_manager_ = manager;
    current_session_key_ = session_key;
    if (!manager) return;

    tools_["spawn_subagent"] = [this](const nlohmann::json& params) -> std::string {
        if (!subagent_manager_) throw std::runtime_error("Subagent manager not configured");

        SpawnParams sp;
        sp.task = params.value("task", "");
        if (sp.task.empty()) throw std::runtime_error("Missing required parameter: task");
        sp.label           = params.value("label", "");
        sp.agent_id        = params.value("agent_id", "");
        sp.model           = params.value("model", "");
        sp.thinking        = params.value("thinking", "off");
        sp.timeout_seconds = params.value("timeout", 300);
        sp.mode            = spawn_mode_from_string(params.value("mode", "run"));
        sp.cleanup         = params.value("cleanup", true);

        auto result = subagent_manager_->Spawn(sp, current_session_key_);

        nlohmann::json r;
        r["status"] = (result.status == SpawnResult::kAccepted)  ? "accepted" :
                      (result.status == SpawnResult::kForbidden) ? "forbidden" : "error";
        if (!result.child_session_key.empty()) r["child_session_key"] = result.child_session_key;
        if (!result.run_id.empty())            r["run_id"]  = result.run_id;
        r["mode"] = spawn_mode_to_string(result.mode);
        if (!result.note.empty())  r["note"]  = result.note;
        if (!result.error.empty()) r["error"] = result.error;
        return r.dump();
    };

    nlohmann::json sp;
    sp["type"] = "object";
    sp["properties"] = {
        {"task",      {{"type", "string"},  {"description", "Task for the subagent"}}},
        {"label",     {{"type", "string"},  {"description", "Human-readable label"}}},
        {"agent_id",  {{"type", "string"},  {"description", "Target agent ID"}}},
        {"model",     {{"type", "string"},  {"description", "Model override"}}},
        {"thinking",  {{"type", "string"},  {"description", "Thinking level: off|low|medium|high"}}},
        {"timeout",   {{"type", "integer"}, {"description", "Timeout in seconds"}}},
        {"mode",      {{"type", "string"},  {"enum", {"run", "session"}}}},
        {"cleanup",   {{"type", "boolean"}, {"description", "Auto-delete on completion"}}}
    };
    sp["required"] = {"task"};

    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "spawn_subagent"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({
        "spawn_subagent",
        "Spawn a subagent to handle a subtask autonomously.",
        sp
    });
    logger_->info("Subagent manager set, spawn_subagent tool registered");
}

// ---------------------------------------------------------------------------
// SetCronScheduler — registers cron agent tool
// ---------------------------------------------------------------------------

void ToolRegistry::SetCronScheduler(std::shared_ptr<CronScheduler> sched) {
    cron_scheduler_ = std::move(sched);
    if (!cron_scheduler_) return;

    tools_["cron"] = [this](const nlohmann::json& params) -> std::string {
        if (!cron_scheduler_) throw std::runtime_error("Cron scheduler not available");
        std::string action = params.value("action", "list");

        if (action == "list" || action == "status") {
            auto jobs = cron_scheduler_->ListJobs();
            nlohmann::json result = nlohmann::json::array();
            for (const auto& job : jobs) {
                nlohmann::json entry;
                entry["id"]       = job.id;
                entry["name"]     = job.name;
                entry["schedule"] = job.schedule;
                entry["message"]  = job.message;
                entry["enabled"]  = job.enabled;
                result.push_back(entry);
            }
            return nlohmann::json{{"jobs", result}, {"count", result.size()}}.dump();
        }

        if (action == "add") {
            std::string name     = params.value("name", "");
            std::string schedule = params.value("schedule", "");
            std::string message  = params.value("message", "");
            std::string sess_key = params.value("sessionKey", "agent:main:main");
            if (schedule.empty() || message.empty())
                throw std::runtime_error("schedule and message are required for cron add");
            if (name.empty()) name = message.substr(0, 40);
            auto id = cron_scheduler_->AddJob(name, schedule, message, sess_key);
            return nlohmann::json{{"ok", true}, {"id", id}}.dump();
        }

        if (action == "remove") {
            std::string id = params.value("id", "");
            if (id.empty()) throw std::runtime_error("id is required for cron remove");
            bool ok = cron_scheduler_->RemoveJob(id);
            return nlohmann::json{{"ok", ok}}.dump();
        }

        if (action == "run") {
            std::string id = params.value("id", "");
            if (id.empty()) throw std::runtime_error("id is required for cron run");
            // Trigger is handled externally; report intent only
            return nlohmann::json{{"ok", true}, {"triggered", id}}.dump();
        }

        throw std::runtime_error("Unknown cron action: " + action);
    };

    nlohmann::json cron_params;
    cron_params["type"] = "object";
    cron_params["properties"] = {
        {"action",     {{"type", "string"}, {"enum", {"list", "status", "add", "remove", "run"}}, {"description", "Action"}}},
        {"id",         {{"type", "string"}, {"description", "Job ID (for remove/run)"}}},
        {"name",       {{"type", "string"}, {"description", "Job name"}}},
        {"schedule",   {{"type", "string"}, {"description", "Cron expression (e.g. '*/5 * * * *')"}}},
        {"message",    {{"type", "string"}, {"description", "Message to send to agent"}}},
        {"sessionKey", {{"type", "string"}, {"description", "Target session key"}}}
    };
    cron_params["required"] = {"action"};

    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "cron"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({"cron",
        "Manage gateway cron jobs: list, add, remove, run.",
        cron_params});

    logger_->info("Cron scheduler set, cron tool registered");
}

// ---------------------------------------------------------------------------
// SetSessionManager — registers sessions_list / sessions_history / sessions_send
// ---------------------------------------------------------------------------

void ToolRegistry::SetSessionManager(std::shared_ptr<SessionManager> mgr) {
    session_manager_ = std::move(mgr);
    if (!session_manager_) return;

    // sessions_list
    tools_["sessions_list"] = [this](const nlohmann::json& params) -> std::string {
        if (!session_manager_) throw std::runtime_error("Session manager not available");
        int limit        = params.value("limit", 20);
        int offset       = params.value("offset", 0);
        auto sessions    = session_manager_->ListSessions();
        int total        = static_cast<int>(sessions.size());
        int start        = std::min(offset, total);
        int end          = std::min(start + limit, total);
        nlohmann::json rows = nlohmann::json::array();
        for (int i = start; i < end; ++i) {
            nlohmann::json row;
            row["key"]         = sessions[i].session_key;
            row["sessionId"]   = sessions[i].session_id;
            row["displayName"] = sessions[i].display_name;
            row["surface"]     = sessions[i].channel.empty() ? "cli" : sessions[i].channel;
            rows.push_back(row);
        }
        return nlohmann::json{{"sessions", rows}, {"total", total}}.dump();
    };
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "sessions_list"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({"sessions_list", "List agent sessions.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"limit":{"type":"integer","description":"Max results (default 20)"},"offset":{"type":"integer","description":"Offset for pagination"}}})JSON")});

    // sessions_history
    tools_["sessions_history"] = [this](const nlohmann::json& params) -> std::string {
        if (!session_manager_) throw std::runtime_error("Session manager not available");
        std::string key = params.value("sessionKey", "");
        if (key.empty()) throw std::runtime_error("sessionKey is required");
        int limit = params.value("limit", 50);
        auto history = session_manager_->GetHistory(key, limit);
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : history) {
            nlohmann::json entry;
            entry["role"]      = msg.role;
            entry["timestamp"] = msg.timestamp;
            nlohmann::json content = nlohmann::json::array();
            for (const auto& block : msg.content) content.push_back(block.ToJson());
            entry["content"] = content;
            messages.push_back(entry);
        }
        return nlohmann::json{{"messages", messages}, {"sessionKey", key}}.dump();
    };
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "sessions_history"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({"sessions_history",
        "Read the transcript of a session.",
        nlohmann::json::parse(R"JSON({"type":"object","properties":{"sessionKey":{"type":"string","description":"Session key"},"limit":{"type":"integer","description":"Max messages (default 50)"}},"required":["sessionKey"]})JSON")});

    // sessions_send
    tools_["sessions_send"] = [this](const nlohmann::json& params) -> std::string {
        if (!session_manager_) throw std::runtime_error("Session manager not available");
        std::string key     = params.value("sessionKey", "");
        std::string message = params.value("message", "");
        if (key.empty() || message.empty())
            throw std::runtime_error("sessionKey and message are required");
        session_manager_->AppendMessage(key, "user", message);
        return nlohmann::json{{"ok", true}, {"sessionKey", key}}.dump();
    };
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [](const ToolSchema& s) { return s.name == "sessions_send"; }),
        tool_schemas_.end());
    tool_schemas_.push_back({"sessions_send",
        "Send a message into another session (agent-to-agent).",
        nlohmann::json::parse(R"({"type":"object","properties":{"sessionKey":{"type":"string","description":"Target session key"},"message":{"type":"string","description":"Message text"}},"required":["sessionKey","message"]})")});

    logger_->info("Session manager set: sessions_list/history/send tools registered");
}

// ---------------------------------------------------------------------------
// RegisterExternalTool
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterExternalTool(const std::string& name,
                                         const std::string& description,
                                         const nlohmann::json& parameters,
                                         std::function<std::string(const nlohmann::json&)> executor) {
    tools_[name] = std::move(executor);
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [&name](const ToolSchema& s) { return s.name == name; }),
        tool_schemas_.end());
    tool_schemas_.push_back({name, description, parameters});
    external_tools_.insert(name);
    logger_->info("Registered external tool: {}", name);
}

// ---------------------------------------------------------------------------
// Permission checks / ExecuteTool / GetToolSchemas / HasTool
// ---------------------------------------------------------------------------

bool ToolRegistry::check_permission(const std::string& tool_name) const {
    if (!permission_checker_) return true;
    if (external_tools_.count(tool_name) && mcp_tool_manager_) {
        return permission_checker_->IsMcpToolAllowed(
            mcp_tool_manager_->GetServerName(tool_name),
            mcp_tool_manager_->GetOriginalToolName(tool_name));
    }
    return permission_checker_->IsAllowed(tool_name);
}

std::string ToolRegistry::ExecuteTool(const std::string& tool_name,
                                       const nlohmann::json& parameters) {
    if (!HasTool(tool_name)) throw std::runtime_error("Tool not found: " + tool_name);
    if (!check_permission(tool_name))
        throw std::runtime_error("Permission denied: tool '" + tool_name + "' is not allowed");
    logger_->debug("Executing tool: {} params: {}", tool_name, parameters.dump());
    try {
        auto result = tools_[tool_name](parameters);
        logger_->debug("Tool {} succeeded", tool_name);
        return result;
    } catch (const std::exception& e) {
        logger_->error("Tool {} failed: {}", tool_name, e.what());
        throw;
    }
}

std::vector<ToolRegistry::ToolSchema> ToolRegistry::GetToolSchemas() const {
    if (!permission_checker_) return tool_schemas_;
    std::vector<ToolSchema> filtered;
    for (const auto& schema : tool_schemas_) {
        if (external_tools_.count(schema.name) && mcp_tool_manager_) {
            if (permission_checker_->IsMcpToolAllowed(
                    mcp_tool_manager_->GetServerName(schema.name),
                    mcp_tool_manager_->GetOriginalToolName(schema.name)))
                filtered.push_back(schema);
        } else {
            if (permission_checker_->IsAllowed(schema.name))
                filtered.push_back(schema);
        }
    }
    return filtered;
}

bool ToolRegistry::HasTool(const std::string& tool_name) const {
    return tools_.find(tool_name) != tools_.end();
}

// ---------------------------------------------------------------------------
// File tools
// ---------------------------------------------------------------------------

std::string ToolRegistry::read_file_tool(const nlohmann::json& params) {
    if (!params.contains("path")) throw std::runtime_error("Missing required parameter: path");
    std::string path = params["path"].get<std::string>();
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace"))
        throw std::runtime_error("Access denied: path outside workspace: " + path);
    if (!std::filesystem::exists(path)) throw std::runtime_error("File not found: " + path);
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to open: " + path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string ToolRegistry::write_file_tool(const nlohmann::json& params) {
    if (!params.contains("path") || !params.contains("content"))
        throw std::runtime_error("Missing required parameters: path, content");
    std::string path    = params["path"].get<std::string>();
    std::string content = params["content"].get<std::string>();
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace"))
        throw std::runtime_error("Access denied: path outside workspace: " + path);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Failed to write: " + path);
    f << content;
    return "Successfully wrote to file: " + path;
}

std::string ToolRegistry::edit_file_tool(const nlohmann::json& params) {
    if (!params.contains("path") || !params.contains("oldText") || !params.contains("newText"))
        throw std::runtime_error("Missing required parameters: path, oldText, newText");
    std::string path     = params["path"].get<std::string>();
    std::string old_text = params["oldText"].get<std::string>();
    std::string new_text = params["newText"].get<std::string>();
    if (!quantclaw::SecuritySandbox::ValidateFilePath(path, "~/.quantclaw/workspace"))
        throw std::runtime_error("Access denied: path outside workspace: " + path);
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Failed to open: " + path);
    std::string content(std::istreambuf_iterator<char>(f), {});
    size_t pos = content.find(old_text);
    if (pos == std::string::npos) throw std::runtime_error("Text not found in file: " + old_text);
    content.replace(pos, old_text.size(), new_text);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Failed to write edited file: " + path);
    out << content;
    return "Successfully edited file: " + path;
}

// ---------------------------------------------------------------------------
// exec_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::exec_tool(const nlohmann::json& params) {
    if (!params.contains("command")) throw std::runtime_error("Missing required parameter: command");
    std::string command = params["command"].get<std::string>();
    int timeout = params.value("timeout", 30);

    if (!quantclaw::SecuritySandbox::ValidateShellCommand(command))
        throw std::runtime_error("Command not allowed: " + command);

    if (approval_manager_) {
        auto decision = approval_manager_->RequestApproval(command);
        if (decision == ApprovalDecision::kDenied)
            throw std::runtime_error("Command execution denied: " + command);
        if (decision == ApprovalDecision::kTimeout)
            throw std::runtime_error("Approval timed out: " + command);
    }

    quantclaw::SecuritySandbox::ApplyResourceLimits();
    logger_->info("Executing command: {}", command);

    auto result = platform::exec_capture(command, timeout);
    if (result.exit_code == -1) throw std::runtime_error("Failed to execute: " + command);
    if (result.exit_code == -2) throw std::runtime_error("Command timeout: " + command);
    if (result.exit_code != 0)
        throw std::runtime_error("Command exited " + std::to_string(result.exit_code) +
                                  ": " + result.output);
    return result.output;
}

// ---------------------------------------------------------------------------
// message_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::message_tool(const nlohmann::json& params) {
    if (!params.contains("channel") || !params.contains("message"))
        throw std::runtime_error("Missing required parameters: channel, message");
    std::string channel = params["channel"].get<std::string>();
    std::string message = params["message"].get<std::string>();
    logger_->info("Message to channel {}: {}", channel, message);
    return "Message sent to channel: " + channel;
}

// ---------------------------------------------------------------------------
// apply_patch_tool
// Supports: *** Begin Patch / *** End Patch wrapper
//   *** Add File: <path>      → create file with content below
//   *** Update File: <path>   → apply unified diff hunks
//   *** Delete File: <path>   → remove file
// ---------------------------------------------------------------------------

std::string ToolRegistry::apply_patch_tool(const nlohmann::json& params) {
    if (!params.contains("patch")) throw std::runtime_error("patch is required");
    std::string patch = params["patch"].get<std::string>();

    // Find Begin/End markers
    const std::string kBegin = "*** Begin Patch";
    const std::string kEnd   = "*** End Patch";
    size_t begin_pos = patch.find(kBegin);
    size_t end_pos   = patch.find(kEnd);
    if (begin_pos == std::string::npos) throw std::runtime_error("Missing '*** Begin Patch' marker");
    std::string body = (end_pos != std::string::npos)
        ? patch.substr(begin_pos + kBegin.size(), end_pos - begin_pos - kBegin.size())
        : patch.substr(begin_pos + kBegin.size());

    // Split into lines
    std::vector<std::string> lines;
    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    int applied = 0;
    std::string current_file;
    enum class FileOp { kNone, kAdd, kUpdate, kDelete } op = FileOp::kNone;
    std::vector<std::string> add_content;
    std::vector<std::string> diff_hunks;

    auto flush_file = [&]() {
        if (current_file.empty()) return;
        if (op == FileOp::kAdd) {
            std::filesystem::create_directories(
                std::filesystem::path(current_file).parent_path());
            std::ofstream f(current_file);
            for (const auto& l : add_content) f << l << "\n";
            ++applied;
        } else if (op == FileOp::kDelete) {
            if (std::filesystem::exists(current_file))
                std::filesystem::remove(current_file);
            ++applied;
        } else if (op == FileOp::kUpdate && !diff_hunks.empty()) {
            // Read current file content
            std::ifstream f(current_file);
            if (!f) throw std::runtime_error("Cannot open for update: " + current_file);
            std::vector<std::string> file_lines;
            std::string fl;
            while (std::getline(f, fl)) {
                if (!fl.empty() && fl.back() == '\r') fl.pop_back();
                file_lines.push_back(fl);
            }
            f.close();

            // Apply hunks (simple line-based application)
            // Each hunk starts with @@ -start,count +start,count @@
            std::vector<std::string> result_lines = file_lines;
            int line_offset = 0;
            size_t i = 0;
            while (i < diff_hunks.size()) {
                std::string& hl = diff_hunks[i];
                if (hl.size() >= 2 && hl.substr(0, 2) == "@@") {
                    // Parse @@ -old_start,old_count +new_start,new_count @@
                    int old_start = 0;
                    int old_count = 0;
                    sscanf(hl.c_str(), "@@ -%d,%d", &old_start, &old_count);
                    int apply_at = old_start - 1 + line_offset;  // 0-indexed

                    // Collect hunk lines
                    std::vector<std::string> removed, added;
                    size_t j = i + 1;
                    while (j < diff_hunks.size() && diff_hunks[j].size() >= 2 &&
                           diff_hunks[j].substr(0, 2) != "@@") {
                        char ch = diff_hunks[j][0];
                        std::string content = diff_hunks[j].substr(1);
                        if (ch == '-') removed.push_back(content);
                        else if (ch == '+') added.push_back(content);
                        ++j;
                    }

                    // Splice: replace removed lines with added lines
                    if (apply_at >= 0 && apply_at <= static_cast<int>(result_lines.size())) {
                        result_lines.erase(result_lines.begin() + apply_at,
                                           result_lines.begin() + apply_at +
                                           static_cast<int>(removed.size()));
                        result_lines.insert(result_lines.begin() + apply_at,
                                            added.begin(), added.end());
                        line_offset += static_cast<int>(added.size()) -
                                       static_cast<int>(removed.size());
                    }
                    i = j;
                } else {
                    ++i;
                }
            }

            std::ofstream out(current_file);
            if (!out) throw std::runtime_error("Cannot write: " + current_file);
            for (const auto& l : result_lines) out << l << "\n";
            ++applied;
        }

        current_file.clear();
        op = FileOp::kNone;
        add_content.clear();
        diff_hunks.clear();
    };

    for (const auto& l : lines) {
        if (l.substr(0, 16) == "*** Add File: ") {
            flush_file();
            current_file = l.substr(14);
            op = FileOp::kAdd;
        } else if (l.substr(0, 19) == "*** Update File: ") {
            flush_file();
            current_file = l.substr(17);
            op = FileOp::kUpdate;
        } else if (l.substr(0, 19) == "*** Delete File: ") {
            flush_file();
            current_file = l.substr(17);
            op = FileOp::kDelete;
        } else if (op == FileOp::kAdd) {
            add_content.push_back(l);
        } else if (op == FileOp::kUpdate) {
            diff_hunks.push_back(l);
        }
    }
    flush_file();

    return "Applied patch: " + std::to_string(applied) + " file(s) modified.";
}

// ---------------------------------------------------------------------------
// process_tool — background process management
// ---------------------------------------------------------------------------

std::string ToolRegistry::process_tool(const nlohmann::json& params) {
    std::string action = params.value("action", "list");

    if (action == "list") {
        std::lock_guard<std::mutex> lk(bg_mu_);
        nlohmann::json sessions = nlohmann::json::array();
        for (auto& [sid, sess] : bg_sessions_) {
            // Poll future without blocking
            if (!sess->exited && sess->future.valid() &&
                sess->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try { sess->output = sess->future.get(); } catch (const std::exception& e) {
                    sess->error = e.what();
                }
                sess->exited = true;
            }
            sessions.push_back({
                {"id",      sid},
                {"command", sess->command},
                {"running", !sess->exited},
                {"error",   sess->error}
            });
        }
        return nlohmann::json{{"sessions", sessions}}.dump();
    }

    if (action == "start") {
        std::string command = params.value("command", "");
        if (command.empty()) throw std::runtime_error("command is required for process start");

        auto sess  = std::make_shared<BgSession>();
        sess->id      = generate_id("proc");
        sess->command = command;
        auto started  = std::chrono::system_clock::now();
        sess->started_at = started;

        // Run command asynchronously
        sess->future = std::async(std::launch::async, [command]() -> std::string {
            auto r = platform::exec_capture(command, 300);  // 5-minute max
            if (r.exit_code != 0 && r.exit_code != -1 && r.exit_code != -2) {
                return r.output + "\n[exit " + std::to_string(r.exit_code) + "]";
            }
            return r.output;
        });

        std::string id = sess->id;
        {
            std::lock_guard<std::mutex> lk(bg_mu_);
            bg_sessions_[id] = std::move(sess);
        }
        return nlohmann::json{{"ok", true}, {"id", id}}.dump();
    }

    std::string id = params.value("id", "");
    if (id.empty()) throw std::runtime_error("id is required for action: " + action);

    std::shared_ptr<BgSession> sess;
    {
        std::lock_guard<std::mutex> lk(bg_mu_);
        auto it = bg_sessions_.find(id);
        if (it == bg_sessions_.end())
            throw std::runtime_error("No session with id: " + id);
        sess = it->second;
    }

    if (action == "log" || action == "poll") {
        int timeout_ms = params.value("timeout", 5000);
        if (!sess->exited && sess->future.valid()) {
            auto status = sess->future.wait_for(std::chrono::milliseconds(timeout_ms));
            if (status == std::future_status::ready) {
                try { sess->output = sess->future.get(); } catch (const std::exception& e) {
                    sess->error = e.what();
                }
                sess->exited = true;
            }
        }
        return nlohmann::json{
            {"id",      id},
            {"running", !sess->exited},
            {"output",  sess->output},
            {"error",   sess->error}
        }.dump();
    }

    if (action == "kill") {
        // Best-effort: mark as done
        sess->exited = true;
        sess->error  = "killed by user";
        return nlohmann::json{{"ok", true}, {"id", id}}.dump();
    }

    if (action == "clear") {
        sess->output.clear();
        return nlohmann::json{{"ok", true}}.dump();
    }

    if (action == "remove") {
        std::lock_guard<std::mutex> lk(bg_mu_);
        bg_sessions_.erase(id);
        return nlohmann::json{{"ok", true}}.dump();
    }

    if (action == "write" || action == "send-keys") {
        // Cannot write to stdin without pipe infrastructure; acknowledge gracefully
        logger_->warn("process send-keys: stdin write not supported in this build");
        return nlohmann::json{{"ok", false}, {"note", "stdin write not supported"}}.dump();
    }

    throw std::runtime_error("Unknown process action: " + action);
}

// ---------------------------------------------------------------------------
// web_search_tool — Cascade: Brave → Tavily → Perplexity → DuckDuckGo → Grok
// ---------------------------------------------------------------------------

std::string ToolRegistry::web_search_tool(const nlohmann::json& params) {
    std::string query     = params.value("query", "");
    int count             = std::clamp(params.value("count", 5), 1, 10);
    std::string freshness = params.value("freshness", "");
    if (query.empty()) throw std::runtime_error("query is required");

    // Determine provider and API keys
    const char* brave_key  = std::getenv("BRAVE_API_KEY");
    const char* tavily_key = std::getenv("TAVILY_API_KEY");
    const char* perp_key   = std::getenv("PERPLEXITY_API_KEY");
    const char* xai_key    = std::getenv("XAI_API_KEY");

    std::string last_error;

    // --- Brave Search ---
    if (brave_key && *brave_key) {
        try {
            std::string path = "/res/v1/web/search?q=" + url_encode(query) +
                               "&count=" + std::to_string(count);
            if (!freshness.empty()) path += "&freshness=" + freshness;

            httplib::SSLClient cli("api.search.brave.com");
            cli.set_default_headers({
                {"Accept",               "application/json"},
                {"Accept-Encoding",      "identity"},
                {"X-Subscription-Token", brave_key}
            });
            cli.set_connection_timeout(10);
            cli.set_read_timeout(15);

            auto res = cli.Get(path);
            if (!res) throw std::runtime_error("Brave Search: connection failed");
            if (res->status != 200)
                throw std::runtime_error("Brave Search HTTP " + std::to_string(res->status));

            auto j = nlohmann::json::parse(res->body);
            nlohmann::json results = nlohmann::json::array();
            if (j.contains("web") && j["web"].contains("results")) {
                for (const auto& r : j["web"]["results"]) {
                    nlohmann::json item;
                    item["title"]       = r.value("title", "");
                    item["url"]         = r.value("url", "");
                    item["description"] = r.value("description", "");
                    results.push_back(item);
                }
            }
            return nlohmann::json{{"provider", "brave"}, {"query", query},
                                   {"results", results}}.dump();
        } catch (const std::exception& e) {
            last_error = std::string("Brave: ") + e.what();
        }
    }

    // --- Tavily Search ---
    if (tavily_key && *tavily_key) {
        try {
            nlohmann::json body = {
                {"api_key",      tavily_key},
                {"query",        query},
                {"max_results",  count},
                {"search_depth", "basic"}
            };
            std::string body_str = body.dump();

            httplib::SSLClient cli("api.tavily.com");
            cli.set_default_headers({{"Content-Type", "application/json"}});
            cli.set_connection_timeout(10);
            cli.set_read_timeout(15);

            auto res = cli.Post("/search", body_str, "application/json");
            if (!res) throw std::runtime_error("Tavily: connection failed");
            if (res->status != 200)
                throw std::runtime_error("Tavily HTTP " + std::to_string(res->status));

            auto j = nlohmann::json::parse(res->body);
            nlohmann::json results = nlohmann::json::array();
            if (j.contains("results")) {
                for (const auto& r : j["results"]) {
                    nlohmann::json item;
                    item["title"]       = r.value("title", "");
                    item["url"]         = r.value("url", "");
                    item["description"] = r.value("content", "");
                    results.push_back(item);
                }
            }
            return nlohmann::json{{"provider", "tavily"}, {"query", query},
                                   {"results", results}}.dump();
        } catch (const std::exception& e) {
            last_error = std::string("Tavily: ") + e.what();
        }
    }

    // --- Perplexity Sonar (OpenAI-compatible) ---
    if (perp_key && *perp_key) {
        try {
            nlohmann::json body = {
                {"model",    "perplexity/sonar"},
                {"messages", nlohmann::json::array({
                    nlohmann::json{{"role", "user"}, {"content", query}}
                })},
                {"max_tokens", 1024}
            };
            std::string body_str = body.dump();

            httplib::SSLClient cli("api.perplexity.ai");
            cli.set_default_headers({
                {"Authorization", std::string("Bearer ") + perp_key},
                {"Content-Type",  "application/json"}
            });
            cli.set_connection_timeout(15);
            cli.set_read_timeout(30);

            auto res = cli.Post("/chat/completions", body_str, "application/json");
            if (!res) throw std::runtime_error("Perplexity: connection failed");
            if (res->status != 200)
                throw std::runtime_error("Perplexity HTTP " + std::to_string(res->status));

            auto j = nlohmann::json::parse(res->body);
            std::string answer;
            if (j.contains("choices") && !j["choices"].empty()) {
                answer = j["choices"][0]["message"].value("content", "");
            }
            nlohmann::json results = nlohmann::json::array();
            nlohmann::json perp_item;
            perp_item["title"]       = "Perplexity Answer";
            perp_item["url"]         = "";
            perp_item["description"] = answer;
            results.push_back(perp_item);
            return nlohmann::json{{"provider", "perplexity"}, {"query", query},
                                   {"results", results}}.dump();
        } catch (const std::exception& e) {
            last_error = std::string("Perplexity: ") + e.what();
        }
    }

    // --- DuckDuckGo HTML scraping (no API key needed) ---
    try {
        std::string ddg_path = "/html/?q=" + url_encode(query);

        httplib::SSLClient cli("html.duckduckgo.com");
        cli.set_default_headers({
            {"User-Agent", "QuantClaw/1.0"},
            {"Accept",     "text/html"}
        });
        cli.set_connection_timeout(10);
        cli.set_read_timeout(15);

        auto res = cli.Get(ddg_path);
        if (res && res->status == 200 && !res->body.empty()) {
            nlohmann::json results = nlohmann::json::array();
            // Extract result links: <a rel="nofollow" class="result__a" href="URL">TITLE</a>
            std::regex link_re(R"REGEX(<a\s+[^>]*class="result__a"[^>]*href="([^"]*)"[^>]*>([\s\S]*?)</a>)REGEX",
                               std::regex::icase);
            // Extract snippets: <a class="result__snippet" ...>DESCRIPTION</a>
            std::regex snippet_re(R"REGEX(<a\s+[^>]*class="result__snippet"[^>]*>([\s\S]*?)</a>)REGEX",
                                  std::regex::icase);
            std::vector<std::pair<std::string, std::string>> links; // url, title
            std::vector<std::string> snippets;

            auto link_begin = std::sregex_iterator(res->body.begin(), res->body.end(), link_re);
            auto link_end   = std::sregex_iterator();
            for (auto it = link_begin; it != link_end; ++it) {
                std::string url   = (*it)[1].str();
                std::string title = html_to_text((*it)[2].str());
                // DuckDuckGo wraps URLs in a redirect; extract actual URL if present
                if (url.find("uddg=") != std::string::npos) {
                    size_t uddg = url.find("uddg=") + 5;
                    size_t amp  = url.find('&', uddg);
                    std::string encoded = (amp != std::string::npos) ?
                        url.substr(uddg, amp - uddg) : url.substr(uddg);
                    // Simple URL-decode for %XX
                    std::ostringstream decoded;
                    size_t i = 0;
                    while (i < encoded.size()) {
                        if (encoded[i] == '%' && i + 2 < encoded.size()) {
                            int hi = 0;
                            if (std::sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &hi) == 1) {
                                decoded << static_cast<char>(hi);
                                i += 3; // skip '%' and its two hex digits
                                continue;
                            } else {
                                decoded << encoded[i];
                                ++i;
                            }
                        } else if (encoded[i] == '+') {
                            decoded << ' ';
                            ++i;
                        } else {
                            decoded << encoded[i];
                            ++i;
                        }
                    }
                    url = decoded.str();
                }
                links.emplace_back(url, title);
            }

            auto snip_begin = std::sregex_iterator(res->body.begin(), res->body.end(), snippet_re);
            auto snip_end   = std::sregex_iterator();
            for (auto it = snip_begin; it != snip_end; ++it) {
                snippets.push_back(html_to_text((*it)[1].str()));
            }

            int n = std::min(count, static_cast<int>(links.size()));
            for (int i = 0; i < n; ++i) {
                nlohmann::json item;
                item["title"]       = links[i].second;
                item["url"]         = links[i].first;
                item["description"] = (i < static_cast<int>(snippets.size())) ?
                    snippets[i] : "";
                results.push_back(item);
            }
            if (!results.empty()) {
                return nlohmann::json{{"provider", "duckduckgo"}, {"query", query},
                                       {"results", results}}.dump();
            }
        }
    } catch (const std::exception& e) {
        last_error = std::string("DuckDuckGo: ") + e.what();
    }

    // --- xAI Grok ---
    if (xai_key && *xai_key) {
        try {
            nlohmann::json body = {
                {"model",    "grok-3-mini"},
                {"messages", nlohmann::json::array({
                    nlohmann::json{{"role", "user"}, {"content", query}}
                })},
                {"max_tokens", 1024}
            };
            std::string body_str = body.dump();

            httplib::SSLClient cli("api.x.ai");
            cli.set_default_headers({
                {"Authorization", std::string("Bearer ") + xai_key},
                {"Content-Type",  "application/json"}
            });
            cli.set_connection_timeout(10);
            cli.set_read_timeout(30);

            auto res = cli.Post("/v1/chat/completions", body_str, "application/json");
            if (!res) throw std::runtime_error("xAI: connection failed");
            if (res->status != 200)
                throw std::runtime_error("xAI HTTP " + std::to_string(res->status));

            auto j = nlohmann::json::parse(res->body);
            std::string answer;
            if (j.contains("choices") && !j["choices"].empty()) {
                answer = j["choices"][0]["message"].value("content", "");
            }
            nlohmann::json results = nlohmann::json::array();
            nlohmann::json grok_item;
            grok_item["title"]       = "Grok Answer";
            grok_item["url"]         = "";
            grok_item["description"] = answer;
            results.push_back(grok_item);
            return nlohmann::json{{"provider", "grok"}, {"query", query},
                                   {"results", results}}.dump();
        } catch (const std::exception& e) {
            last_error = std::string("Grok: ") + e.what();
        }
    }

    // All providers failed
    std::string error_msg = "web_search: no provider succeeded.";
    if (!last_error.empty()) {
        error_msg += " Last error: " + last_error;
    }
    error_msg += " Configure BRAVE_API_KEY, TAVILY_API_KEY, PERPLEXITY_API_KEY, or XAI_API_KEY. DuckDuckGo requires no key.";
    throw std::runtime_error(error_msg);
}

// ---------------------------------------------------------------------------
// web_fetch_tool — HTTP GET + HTML-to-text
// ---------------------------------------------------------------------------

std::string ToolRegistry::web_fetch_tool(const nlohmann::json& params) {
    std::string url_str = params.value("url", "");
    int max_chars       = params.value("maxChars", 50000);
    if (url_str.empty()) throw std::runtime_error("url is required");

    // Parse URL — find scheme and host
    bool is_https = url_str.substr(0, 8) == "https://";
    bool is_http  = url_str.substr(0, 7) == "http://";
    if (!is_https && !is_http)
        throw std::runtime_error("Only http:// and https:// URLs are supported");

    std::string stripped = is_https ? url_str.substr(8) : url_str.substr(7);
    size_t slash_pos = stripped.find('/');
    std::string host = (slash_pos != std::string::npos) ? stripped.substr(0, slash_pos) : stripped;
    std::string path = (slash_pos != std::string::npos) ? stripped.substr(slash_pos) : "/";

    // SSRF guard — block private ranges
    const std::vector<std::string> kBlocked = {
        "localhost", "127.", "0.0.0.0", "::1", "10.", "192.168.", "169.254."
    };
    for (const auto& b : kBlocked) {
        if (host.find(b) != std::string::npos) {
            throw std::runtime_error("SSRF guard: blocked host " + host);
        }
    }

    httplib::Headers headers = {
        {"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/122.0.0.0 Safari/537.36"},
        {"Accept",     "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
        {"Accept-Language", "en-US,en;q=0.9"}
    };

    std::string body;
    std::string content_type;

    auto handle_response = [&](const httplib::Response& res) {
        body         = res.body;
        content_type = res.get_header_value("Content-Type");
    };

    if (is_https) {
        httplib::SSLClient cli(host);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(20);
        cli.set_follow_location(true);
        auto res = cli.Get(path, headers);
        if (!res) throw std::runtime_error("web_fetch: connection to " + host + " failed");
        if (res->status >= 400) {
            throw std::runtime_error("web_fetch HTTP " + std::to_string(res->status));
        }
        handle_response(*res);
    } else {
        httplib::Client cli(host);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(20);
        cli.set_follow_location(true);
        auto res = cli.Get(path, headers);
        if (!res) throw std::runtime_error("web_fetch: connection to " + host + " failed");
        if (res->status >= 400) {
            throw std::runtime_error("web_fetch HTTP " + std::to_string(res->status));
        }
        handle_response(*res);
    }

    // Convert content to text
    std::string text;
    if (content_type.find("html") != std::string::npos) {
        text = html_to_text(body);
    } else if (content_type.find("json") != std::string::npos) {
        try {
            auto j = nlohmann::json::parse(body);
            text = j.dump(2);
        } catch (...) {
            text = body;
        }
    } else {
        text = body;
    }

    // Truncate
    if (static_cast<int>(text.size()) > max_chars) {
        text = text.substr(0, max_chars) + "\n\n[truncated at " +
               std::to_string(max_chars) + " chars]";
    }

    return nlohmann::json{
        {"url",         url_str},
        {"content",     text},
        {"contentType", content_type}
    }.dump();
}

// ---------------------------------------------------------------------------
// memory_search_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::memory_search_tool(const nlohmann::json& params) {
    std::string query   = params.value("query", "");
    int max_results     = params.value("maxResults", 10);
    if (query.empty()) throw std::runtime_error("query is required");

    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    auto workspace = std::filesystem::path(home_str) / ".quantclaw/agents/main/workspace";

    MemorySearch search(logger_);
    search.IndexDirectory(workspace);
    auto results = search.Search(query, max_results);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : results) {
        nlohmann::json entry;
        entry["source"]     = r.source;
        entry["content"]    = r.content;
        entry["score"]      = r.score;
        entry["lineNumber"] = r.line_number;
        arr.push_back(entry);
    }
    return nlohmann::json{{"results", arr}, {"count", arr.size()}, {"query", query}}.dump();
}

// ---------------------------------------------------------------------------
// memory_get_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::memory_get_tool(const nlohmann::json& params) {
    std::string rel_path = params.value("path", "");
    if (rel_path.empty()) throw std::runtime_error("path is required");

    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    auto workspace = std::filesystem::path(home_str) / ".quantclaw/agents/main/workspace";
    auto full_path = workspace / rel_path;

    // Security: must remain inside workspace
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto ws_canon  = std::filesystem::weakly_canonical(workspace);
    if (canonical.string().substr(0, ws_canon.string().size()) != ws_canon.string()) {
        throw std::runtime_error("Access denied: path outside workspace");
    }

    if (!std::filesystem::exists(full_path)) {
        throw std::runtime_error("File not found: " + rel_path);
    }

    std::ifstream f(full_path);
    if (!f) throw std::runtime_error("Cannot read: " + rel_path);
    std::string content(std::istreambuf_iterator<char>(f), {});
    return nlohmann::json{{"path", rel_path}, {"content", content}}.dump();
}

} // namespace quantclaw
