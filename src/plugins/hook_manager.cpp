// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/plugins/hook_manager.hpp"

#include <algorithm>
#include <future>
#include <thread>
#include <unordered_map>

#include "quantclaw/plugins/sidecar_manager.hpp"

namespace quantclaw {

// Hook mode classification — matches OpenClaw exactly.
static const std::unordered_map<std::string, HookMode> kHookModes = {
    // Modifying hooks: sequential, results merged
    {hooks::kBeforeModelResolve, HookMode::kModifying},
    {hooks::kBeforePromptBuild, HookMode::kModifying},
    {hooks::kBeforeAgentStart, HookMode::kModifying},
    {hooks::kMessageSending, HookMode::kModifying},
    {hooks::kBeforeToolCall, HookMode::kModifying},
    {hooks::kSubagentSpawning, HookMode::kModifying},
    {hooks::kSubagentDeliveryTarget, HookMode::kModifying},

    // Sync hooks: synchronous only, for hot paths
    {hooks::kToolResultPersist, HookMode::kSync},
    {hooks::kBeforeMessageWrite, HookMode::kSync},

    // All others are void (fire-and-forget, parallel)
    {hooks::kLlmInput, HookMode::kVoid},
    {hooks::kLlmOutput, HookMode::kVoid},
    {hooks::kAgentEnd, HookMode::kVoid},
    {hooks::kBeforeCompaction, HookMode::kVoid},
    {hooks::kAfterCompaction, HookMode::kVoid},
    {hooks::kBeforeReset, HookMode::kVoid},
    {hooks::kMessageReceived, HookMode::kVoid},
    {hooks::kMessageSent, HookMode::kVoid},
    {hooks::kAfterToolCall, HookMode::kVoid},
    {hooks::kSessionStart, HookMode::kVoid},
    {hooks::kSessionEnd, HookMode::kVoid},
    {hooks::kSubagentSpawned, HookMode::kVoid},
    {hooks::kSubagentEnded, HookMode::kVoid},
    {hooks::kGatewayStart, HookMode::kVoid},
    {hooks::kGatewayStop, HookMode::kVoid},
};

HookMode GetHookMode(const std::string& hook_name) {
  auto it = kHookModes.find(hook_name);
  if (it != kHookModes.end())
    return it->second;
  return HookMode::kVoid;  // Unknown hooks default to void
}

// HookManager

HookManager::HookManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void HookManager::RegisterHook(const std::string& hook_name,
                               const std::string& plugin_id,
                               HookHandler handler, int priority) {
  std::lock_guard<std::mutex> lock(mu_);
  auto& handlers = hooks_[hook_name];
  handlers.push_back({plugin_id, hook_name, std::move(handler), priority});

  // Keep sorted by priority (highest first)
  std::sort(handlers.begin(), handlers.end(),
            [](const HookRegistration& a, const HookRegistration& b) {
              return a.priority > b.priority;
            });
}

void HookManager::SetSidecar(std::shared_ptr<SidecarManager> sidecar) {
  std::lock_guard<std::mutex> lock(mu_);
  sidecar_ = std::move(sidecar);
}

nlohmann::json HookManager::Fire(const std::string& hook_name,
                                 const nlohmann::json& event) {
  HookMode mode = GetHookMode(hook_name);

  // Copy handlers under lock
  std::vector<HookRegistration> handlers;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = hooks_.find(hook_name);
    if (it != hooks_.end()) {
      handlers = it->second;
    }
  }

  switch (mode) {
    case HookMode::kVoid:
      return FireVoid(hook_name, handlers, event);
    case HookMode::kModifying:
      return FireModifying(hook_name, handlers, event);
    case HookMode::kSync:
      return FireSync(hook_name, handlers, event);
  }
  return nlohmann::json::object();
}

void HookManager::FireAsync(const std::string& hook_name,
                            const nlohmann::json& event) {
  auto logger = logger_;
  auto self_hooks = [this, hook_name, event, logger]() {
    try {
      Fire(hook_name, event);
    } catch (const std::exception& e) {
      logger->error("Async hook {} failed: {}", hook_name, e.what());
    }
  };
  std::thread(std::move(self_hooks)).detach();
}

// Void mode: fire-and-forget, parallel execution.
nlohmann::json
HookManager::FireVoid(const std::string& hook_name,
                      const std::vector<HookRegistration>& handlers,
                      const nlohmann::json& event) {
  // Launch native handlers in parallel
  std::vector<std::future<void>> futures;
  for (const auto& reg : handlers) {
    futures.push_back(
        std::async(std::launch::async, [this, &reg, &event, &hook_name]() {
          try {
            reg.handler(event);
          } catch (const std::exception& e) {
            logger_->error("Hook {} handler from {} failed: {}", hook_name,
                           reg.plugin_id, e.what());
          }
        }));
  }

  // Wait for all to complete
  for (auto& f : futures) {
    f.wait();
  }

  // Forward to sidecar (fire-and-forget — we don't use the result)
  ForwardToSidecar(hook_name, event);

  return nlohmann::json::object();
}

// Modifying mode: sequential, results merged via merge_patch.
nlohmann::json
HookManager::FireModifying(const std::string& hook_name,
                           const std::vector<HookRegistration>& handlers,
                           const nlohmann::json& event) {
  nlohmann::json merged_result = nlohmann::json::object();

  // Run native handlers sequentially in priority order
  for (const auto& reg : handlers) {
    try {
      auto result = reg.handler(event);
      if (result.is_object()) {
        merged_result.merge_patch(result);
      }
    } catch (const std::exception& e) {
      logger_->error("Hook {} handler from {} failed: {}", hook_name,
                     reg.plugin_id, e.what());
    }
  }

  // Forward to sidecar and merge result
  auto sidecar_result = ForwardToSidecar(hook_name, event);
  if (sidecar_result.is_object()) {
    merged_result.merge_patch(sidecar_result);
  }

  return merged_result;
}

// Sync mode: synchronous only, for hot paths like tool_result_persist.
nlohmann::json
HookManager::FireSync(const std::string& hook_name,
                      const std::vector<HookRegistration>& handlers,
                      const nlohmann::json& event) {
  nlohmann::json merged_result = nlohmann::json::object();

  // Run native handlers sequentially
  for (const auto& reg : handlers) {
    try {
      auto result = reg.handler(event);
      if (result.is_object()) {
        merged_result.merge_patch(result);
      }
    } catch (const std::exception& e) {
      logger_->error("Hook {} handler from {} failed: {}", hook_name,
                     reg.plugin_id, e.what());
    }
  }

  // Forward to sidecar — sync hooks use short timeout
  auto sidecar_result = ForwardToSidecar(hook_name, event);
  if (sidecar_result.is_object()) {
    merged_result.merge_patch(sidecar_result);
  }

  return merged_result;
}

nlohmann::json HookManager::ForwardToSidecar(const std::string& hook_name,
                                             const nlohmann::json& event) {
  std::shared_ptr<SidecarManager> sidecar;
  {
    std::lock_guard<std::mutex> lock(mu_);
    sidecar = sidecar_;
  }

  if (!sidecar || !sidecar->IsRunning()) {
    return nlohmann::json::object();
  }

  nlohmann::json params = {
      {"hookName", hook_name},
      {"event", event},
  };

  // Use shorter timeout for sync hooks
  HookMode mode = GetHookMode(hook_name);
  int timeout_ms = (mode == HookMode::kSync) ? 5000 : 30000;

  auto resp = sidecar->Call("plugin.hooks", params, timeout_ms);
  if (resp.ok && resp.result.is_object()) {
    return resp.result;
  }
  if (!resp.ok) {
    logger_->warn("Sidecar hook {} failed: {}", hook_name, resp.error);
  }
  return nlohmann::json::object();
}

bool HookManager::UnregisterHook(const std::string& hook_name,
                                 const std::string& plugin_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = hooks_.find(hook_name);
  if (it == hooks_.end())
    return false;

  auto& handlers = it->second;
  auto before = handlers.size();
  handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                [&](const HookRegistration& r) {
                                  return r.plugin_id == plugin_id;
                                }),
                 handlers.end());
  if (handlers.empty()) {
    hooks_.erase(it);
  }
  return handlers.size() < before;
}

void HookManager::Clear() {
  std::lock_guard<std::mutex> lock(mu_);
  hooks_.clear();
}

std::vector<std::string> HookManager::RegisteredHooks() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> names;
  for (const auto& [name, _] : hooks_) {
    names.push_back(name);
  }
  return names;
}

size_t HookManager::HandlerCount(const std::string& hook_name) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = hooks_.find(hook_name);
  if (it == hooks_.end())
    return 0;
  return it->second.size();
}

}  // namespace quantclaw
