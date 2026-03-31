// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.providers.failover_resolver;

import std;

import quantclaw.providers.cooldown_tracker;
import quantclaw.providers.llm_provider;
import quantclaw.providers.provider_error;
import quantclaw.providers.provider_registry;

export namespace quantclaw {

// Auth profile: one API key for a provider.
// A provider may have multiple profiles for key rotation.
struct AuthProfile {
  std::string id;           // e.g. "prod", "backup"
  std::string api_key;      // Direct key value
  std::string api_key_env;  // Env var name (resolved at startup)
  int priority = 0;         // Lower = higher priority (0 is highest)
};

// Result of a failover resolution attempt.
struct ResolvedProvider {
  std::shared_ptr<LLMProvider> provider;
  std::string provider_id;
  std::string profile_id;
  std::string model;
  bool is_fallback = false;  // True if this is not the primary model
};

// Orchestrates multi-profile key rotation and model fallback chains.
class FailoverResolver {
 public:
  FailoverResolver(ProviderRegistry* registry,
                   std::shared_ptr<spdlog::logger> logger);

  void SetFallbackChain(const std::vector<std::string>& models);
  void SetProfiles(const std::string& provider_id,
                   const std::vector<AuthProfile>& profiles);

  std::optional<ResolvedProvider> Resolve(const std::string& model,
                                          const std::string& session_key = "");

  void RecordSuccess(const std::string& provider_id,
                     const std::string& profile_id,
                     const std::string& session_key = "");

  void RecordFailure(const std::string& provider_id,
                     const std::string& profile_id, ProviderErrorKind kind,
                     int retry_after_seconds = 0);

  void ClearSessionPin(const std::string& session_key);

  const CooldownTracker& GetCooldownTracker() const {
    return cooldown_;
  }

 private:
  std::string cooldown_key(const std::string& provider_id,
                           const std::string& profile_id) const;

  std::optional<ResolvedProvider>
  try_resolve_model(const std::string& model, const std::string& session_key);

  ProviderRegistry* registry_;
  std::shared_ptr<spdlog::logger> logger_;
  CooldownTracker cooldown_;

  mutable std::mutex mu_;
  std::vector<std::string> fallback_chain_;
  std::unordered_map<std::string, std::vector<AuthProfile>> profiles_;
  std::unordered_map<std::string, int64_t> profile_last_used_;

  struct SessionPin {
    std::string provider_id;
    std::string profile_id;
  };
  std::unordered_map<std::string, SessionPin> session_pins_;
};

FailoverResolver::FailoverResolver(ProviderRegistry* registry,
                                   std::shared_ptr<spdlog::logger> logger)
    : registry_(registry), logger_(std::move(logger)) {}

void FailoverResolver::SetFallbackChain(
    const std::vector<std::string>& models) {
  std::lock_guard<std::mutex> lock(mu_);
  fallback_chain_ = models;
}

void FailoverResolver::SetProfiles(const std::string& provider_id,
                                   const std::vector<AuthProfile>& profiles) {
  std::lock_guard<std::mutex> lock(mu_);
  profiles_[provider_id] = profiles;
}

std::optional<ResolvedProvider>
FailoverResolver::Resolve(const std::string& model,
                          const std::string& session_key) {
  auto result = try_resolve_model(model, session_key);
  if (result)
    return result;

  std::vector<std::string> chain_snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    chain_snapshot = fallback_chain_;
  }

  for (const auto& fallback_model : chain_snapshot) {
    if (fallback_model == model)
      continue;

    result = try_resolve_model(fallback_model, session_key);
    if (result) {
      result->is_fallback = true;
      logger_->warn("Primary model '{}' unavailable, falling back to '{}'",
                    model, fallback_model);
      return result;
    }
  }

  logger_->error("All models exhausted (primary='{}', {} fallbacks)", model,
                 chain_snapshot.size());
  return std::nullopt;
}

void FailoverResolver::RecordSuccess(const std::string& provider_id,
                                     const std::string& profile_id,
                                     const std::string& session_key) {
  auto key = cooldown_key(provider_id, profile_id);
  cooldown_.RecordSuccess(key);

  {
    std::lock_guard<std::mutex> lock(mu_);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    profile_last_used_[key] = now;

    if (!session_key.empty()) {
      session_pins_[session_key] = {provider_id, profile_id};
    }
  }
}

void FailoverResolver::RecordFailure(const std::string& provider_id,
                                     const std::string& profile_id,
                                     ProviderErrorKind kind,
                                     int retry_after_seconds) {
  cooldown_.RecordFailure(cooldown_key(provider_id, profile_id), kind,
                          retry_after_seconds);
  logger_->warn("Provider {}:{} failed ({}), cooldown set{}", provider_id,
                profile_id, ProviderErrorKindToString(kind),
                retry_after_seconds > 0
                    ? " (Retry-After: " + std::to_string(retry_after_seconds) +
                          "s)"
                    : "");
}

void FailoverResolver::ClearSessionPin(const std::string& session_key) {
  std::lock_guard<std::mutex> lock(mu_);
  session_pins_.erase(session_key);
}

std::string
FailoverResolver::cooldown_key(const std::string& provider_id,
                               const std::string& profile_id) const {
  if (profile_id.empty())
    return provider_id;
  return provider_id + ":" + profile_id;
}

std::optional<ResolvedProvider>
FailoverResolver::try_resolve_model(const std::string& model,
                                    const std::string& session_key) {
  auto ref = registry_->ResolveModel(model);
  const std::string& provider_id = ref.provider;

  std::lock_guard<std::mutex> lock(mu_);

  if (!session_key.empty()) {
    auto pin_it = session_pins_.find(session_key);
    if (pin_it != session_pins_.end() &&
        pin_it->second.provider_id == provider_id) {
      const auto& pin = pin_it->second;
      auto key = cooldown_key(provider_id, pin.profile_id);
      if (!cooldown_.IsInCooldown(key)) {
        auto prof_it = profiles_.find(provider_id);
        if (prof_it != profiles_.end()) {
          for (const auto& profile : prof_it->second) {
            if (profile.id == pin.profile_id) {
              auto provider =
                  registry_->GetProviderWithKey(provider_id, profile.api_key);
              if (provider) {
                return ResolvedProvider{provider, provider_id, pin.profile_id,
                                        ref.model, false};
              }
            }
          }
        }
      }
      session_pins_.erase(pin_it);
    }
  }

  auto prof_it = profiles_.find(provider_id);
  if (prof_it != profiles_.end() && !prof_it->second.empty()) {
    struct Candidate {
      const AuthProfile* profile;
      bool in_cooldown;
      int priority;
      int64_t last_used;
      int index;
    };
    std::vector<Candidate> available;
    std::vector<Candidate> cooled_down;

    int idx = 0;
    for (const auto& profile : prof_it->second) {
      auto key = cooldown_key(provider_id, profile.id);
      auto lu_it = profile_last_used_.find(key);
      int64_t last_used =
          (lu_it != profile_last_used_.end()) ? lu_it->second : 0;

      if (cooldown_.IsInCooldown(key)) {
        cooled_down.push_back({&profile, true, profile.priority, last_used,
                               idx});
      } else {
        available.push_back({&profile, false, profile.priority, last_used,
                               idx});
      }
      idx++;
    }

    std::sort(available.begin(), available.end(),
              [](const Candidate& a, const Candidate& b) {
                if (a.priority != b.priority)
                  return a.priority < b.priority;
                if (a.last_used != b.last_used)
                  return a.last_used < b.last_used;
                return a.index < b.index;
              });

    for (const auto& c : available) {
      auto provider =
          registry_->GetProviderWithKey(provider_id, c.profile->api_key);
      if (provider) {
        auto key = cooldown_key(provider_id, c.profile->id);
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
        profile_last_used_[key] = now;
        return ResolvedProvider{provider, provider_id, c.profile->id, ref.model,
                                false};
      }
    }

    if (!cooled_down.empty()) {
      auto probe_key = cooldown_key(provider_id, cooled_down[0].profile->id);
      if (cooldown_.TryProbe(probe_key)) {
        logger_->info("Probing cooled-down profile {}:{} (probe throttle)",
                      provider_id, cooled_down[0].profile->id);
        auto provider = registry_->GetProviderWithKey(
            provider_id, cooled_down[0].profile->api_key);
        if (provider) {
          return ResolvedProvider{provider, provider_id,
                                  cooled_down[0].profile->id, ref.model,
                                  false};
        }
      }
    }

    logger_->debug("All profiles for provider '{}' are in cooldown",
                   provider_id);
    return std::nullopt;
  }

  auto key = cooldown_key(provider_id, "");
  if (cooldown_.IsInCooldown(key)) {
    if (cooldown_.TryProbe(key)) {
      logger_->info("Probing cooled-down provider '{}' (probe throttle)",
                    provider_id);
      auto provider = registry_->GetProvider(provider_id);
      if (provider) {
        return ResolvedProvider{provider, provider_id, "", ref.model, false};
      }
    }
    return std::nullopt;
  }

  auto provider = registry_->GetProvider(provider_id);
  if (provider) {
    return ResolvedProvider{provider, provider_id, "", ref.model, false};
  }

  return std::nullopt;
}

}  // namespace quantclaw
