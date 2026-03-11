// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

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

// Hook execution mode — matches OpenClaw semantics.
//   kVoid:      fire-and-forget, all handlers run in parallel
//   kModifying: sequential, results merged via merge_patch
//   kSync:      synchronous only on the hot path (tool_result_persist,
//               before_message_write)
enum class HookMode { kVoid, kModifying, kSync };

// Returns the execution mode for a given hook name.
HookMode GetHookMode(const std::string& hook_name);

class SidecarManager;

// Manages hook registration and execution.
// C++ native hooks run inline; plugin hooks are forwarded to the sidecar.
class HookManager {
 public:
  explicit HookManager(std::shared_ptr<spdlog::logger> logger);

  // Register a native C++ hook handler
  void RegisterHook(const std::string& hook_name, const std::string& plugin_id,
                    HookHandler handler, int priority = 0);

  // Set the sidecar for forwarding hooks to plugins
  void SetSidecar(std::shared_ptr<SidecarManager> sidecar);

  // Fire a hook event with automatic mode dispatch.
  // - void hooks:      handlers run in parallel, no result
  // - modifying hooks: handlers run sequentially, results merged
  // - sync hooks:      handlers run sequentially, sync only
  nlohmann::json Fire(const std::string& hook_name,
                      const nlohmann::json& event);

  // Fire a hook asynchronously (fire-and-forget, always uses void semantics)
  void FireAsync(const std::string& hook_name, const nlohmann::json& event);

  // List all registered hooks
  std::vector<std::string> RegisteredHooks() const;

  // Unregister a specific handler by plugin_id from a hook
  bool UnregisterHook(const std::string& hook_name,
                      const std::string& plugin_id);

  // Clear all registered handlers
  void Clear();

  // Number of handlers for a specific hook
  size_t HandlerCount(const std::string& hook_name) const;

 private:
  // Mode-specific firing methods
  nlohmann::json FireVoid(const std::string& hook_name,
                          const std::vector<HookRegistration>& handlers,
                          const nlohmann::json& event);
  nlohmann::json FireModifying(const std::string& hook_name,
                               const std::vector<HookRegistration>& handlers,
                               const nlohmann::json& event);
  nlohmann::json FireSync(const std::string& hook_name,
                          const std::vector<HookRegistration>& handlers,
                          const nlohmann::json& event);

  // Forward hook to sidecar (returns empty object if no sidecar)
  nlohmann::json ForwardToSidecar(const std::string& hook_name,
                                  const nlohmann::json& event);

  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<SidecarManager> sidecar_;

  mutable std::mutex mu_;
  std::map<std::string, std::vector<HookRegistration>> hooks_;
};

}  // namespace quantclaw
