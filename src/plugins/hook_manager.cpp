#include "quantclaw/plugins/hook_manager.hpp"
#include "quantclaw/plugins/sidecar_manager.hpp"
#include <algorithm>
#include <thread>

namespace quantclaw {

HookManager::HookManager(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void HookManager::register_hook(const std::string& hook_name,
                                const std::string& plugin_id,
                                HookHandler handler,
                                int priority) {
  std::lock_guard<std::mutex> lock(mu_);
  auto& handlers = hooks_[hook_name];
  handlers.push_back({plugin_id, hook_name, std::move(handler), priority});

  // Keep sorted by priority (highest first)
  std::sort(handlers.begin(), handlers.end(),
            [](const HookRegistration& a, const HookRegistration& b) {
              return a.priority > b.priority;
            });
}

void HookManager::set_sidecar(std::shared_ptr<SidecarManager> sidecar) {
  std::lock_guard<std::mutex> lock(mu_);
  sidecar_ = std::move(sidecar);
}

nlohmann::json HookManager::fire(const std::string& hook_name,
                                 const nlohmann::json& event) {
  nlohmann::json merged_result = nlohmann::json::object();

  // Run native handlers
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = hooks_.find(hook_name);
    if (it != hooks_.end()) {
      for (const auto& reg : it->second) {
        try {
          auto result = reg.handler(event);
          if (result.is_object()) {
            merged_result.merge_patch(result);
          }
        } catch (const std::exception& e) {
          logger_->error("Hook {} handler from {} failed: {}",
                         hook_name, reg.plugin_id, e.what());
        }
      }
    }
  }

  // Forward to sidecar plugins
  std::shared_ptr<SidecarManager> sidecar;
  {
    std::lock_guard<std::mutex> lock(mu_);
    sidecar = sidecar_;
  }

  if (sidecar && sidecar->is_running()) {
    nlohmann::json params = {
        {"hookName", hook_name},
        {"event", event},
    };
    auto resp = sidecar->call("plugin.hooks", params);
    if (resp.ok && resp.result.is_object()) {
      merged_result.merge_patch(resp.result);
    } else if (!resp.ok) {
      logger_->warn("Sidecar hook {} failed: {}", hook_name, resp.error);
    }
  }

  return merged_result;
}

void HookManager::fire_async(const std::string& hook_name,
                              const nlohmann::json& event) {
  // Capture by value for thread safety
  auto logger = logger_;
  auto self_hooks = [this, hook_name, event, logger]() {
    try {
      fire(hook_name, event);
    } catch (const std::exception& e) {
      logger->error("Async hook {} failed: {}", hook_name, e.what());
    }
  };
  std::thread(std::move(self_hooks)).detach();
}

std::vector<std::string> HookManager::registered_hooks() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> names;
  for (const auto& [name, _] : hooks_) {
    names.push_back(name);
  }
  return names;
}

size_t HookManager::handler_count(const std::string& hook_name) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = hooks_.find(hook_name);
  if (it == hooks_.end()) return 0;
  return it->second.size();
}

}  // namespace quantclaw
