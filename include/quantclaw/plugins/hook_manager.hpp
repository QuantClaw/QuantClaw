#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

// All hook names matching OpenClaw's 24 hook types
namespace hooks {
constexpr const char* kBeforeModelResolve = "before_model_resolve";
constexpr const char* kBeforePromptBuild = "before_prompt_build";
constexpr const char* kBeforeAgentStart = "before_agent_start";
constexpr const char* kLlmInput = "llm_input";
constexpr const char* kLlmOutput = "llm_output";
constexpr const char* kAgentEnd = "agent_end";
constexpr const char* kBeforeCompaction = "before_compaction";
constexpr const char* kAfterCompaction = "after_compaction";
constexpr const char* kBeforeReset = "before_reset";
constexpr const char* kMessageReceived = "message_received";
constexpr const char* kMessageSending = "message_sending";
constexpr const char* kMessageSent = "message_sent";
constexpr const char* kBeforeToolCall = "before_tool_call";
constexpr const char* kAfterToolCall = "after_tool_call";
constexpr const char* kToolResultPersist = "tool_result_persist";
constexpr const char* kBeforeMessageWrite = "before_message_write";
constexpr const char* kSessionStart = "session_start";
constexpr const char* kSessionEnd = "session_end";
constexpr const char* kSubagentSpawning = "subagent_spawning";
constexpr const char* kSubagentDeliveryTarget = "subagent_delivery_target";
constexpr const char* kSubagentSpawned = "subagent_spawned";
constexpr const char* kSubagentEnded = "subagent_ended";
constexpr const char* kGatewayStart = "gateway_start";
constexpr const char* kGatewayStop = "gateway_stop";
}  // namespace hooks

// A hook handler registered by C++ code or forwarded to sidecar
using HookHandler = std::function<nlohmann::json(const nlohmann::json& event)>;

struct HookRegistration {
  std::string plugin_id;
  std::string hook_name;
  HookHandler handler;
  int priority = 0;  // higher runs first
};

class SidecarManager;

// Manages hook registration and execution.
// C++ native hooks run inline; plugin hooks are forwarded to the sidecar.
class HookManager {
 public:
  explicit HookManager(std::shared_ptr<spdlog::logger> logger);

  // Register a native C++ hook handler
  void register_hook(const std::string& hook_name,
                     const std::string& plugin_id,
                     HookHandler handler,
                     int priority = 0);

  // Set the sidecar for forwarding hooks to plugins
  void set_sidecar(std::shared_ptr<SidecarManager> sidecar);

  // Fire a hook event. Runs all registered handlers (native + sidecar)
  // sorted by priority (highest first). Returns merged results.
  nlohmann::json fire(const std::string& hook_name,
                      const nlohmann::json& event);

  // Fire a hook asynchronously (fire-and-forget)
  void fire_async(const std::string& hook_name,
                  const nlohmann::json& event);

  // List all registered hooks
  std::vector<std::string> registered_hooks() const;

  // Number of handlers for a specific hook
  size_t handler_count(const std::string& hook_name) const;

 private:
  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<SidecarManager> sidecar_;

  mutable std::mutex mu_;
  std::map<std::string, std::vector<HookRegistration>> hooks_;
};

}  // namespace quantclaw
