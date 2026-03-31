// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.plugins.hook_manager;

import std;
import nlohmann.json;
import quantclaw.plugins.sidecar_manager;

export namespace quantclaw {

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

using HookHandler = std::function<nlohmann::json(const nlohmann::json& event)>;

struct HookRegistration {
  std::string plugin_id;
  std::string hook_name;
  HookHandler handler;
  int priority = 0;
};

enum class HookMode { kVoid, kModifying, kSync };

HookMode GetHookMode(const std::string& hook_name);

class HookManager {
 public:
  explicit HookManager(std::shared_ptr<spdlog::logger> logger);

  void RegisterHook(const std::string& hook_name, const std::string& plugin_id,
                    HookHandler handler, int priority = 0);
  void SetSidecar(std::shared_ptr<SidecarManager> sidecar);
  nlohmann::json Fire(const std::string& hook_name,
                      const nlohmann::json& event);
  void FireAsync(const std::string& hook_name, const nlohmann::json& event);
  std::vector<std::string> RegisteredHooks() const;
  bool UnregisterHook(const std::string& hook_name,
                      const std::string& plugin_id);
  void Clear();
  std::size_t HandlerCount(const std::string& hook_name) const;

 private:
  nlohmann::json FireVoid(const std::string& hook_name,
                          const std::vector<HookRegistration>& handlers,
                          const nlohmann::json& event);
  nlohmann::json FireModifying(const std::string& hook_name,
                               const std::vector<HookRegistration>& handlers,
                               const nlohmann::json& event);
  nlohmann::json FireSync(const std::string& hook_name,
                          const std::vector<HookRegistration>& handlers,
                          const nlohmann::json& event);
  nlohmann::json ForwardToSidecar(const std::string& hook_name,
                                  const nlohmann::json& event);

  std::shared_ptr<spdlog::logger> logger_;
  std::shared_ptr<SidecarManager> sidecar_;
  mutable std::mutex mu_;
  std::map<std::string, std::vector<HookRegistration>> hooks_;
};

}  // namespace quantclaw