// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/failover_resolver.hpp"

#include <optional>

#include "quantclaw/providers/provider_registry.hpp"

namespace quantclaw {

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
  // Try the primary model first
  auto result = try_resolve_model(model, session_key);
  if (result)
    return result;

  // Walk the fallback chain
  // Snapshot the fallback chain under lock, then release lock before iterating
  std::vector<std::string> chain_snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    chain_snapshot = fallback_chain_;
  }

  for (const auto& fallback_model : chain_snapshot) {
    // Skip if it's the same as primary
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
  cooldown_.RecordSuccess(cooldown_key(provider_id, profile_id));

  if (!session_key.empty()) {
    std::lock_guard<std::mutex> lock(mu_);
    session_pins_[session_key] = {provider_id, profile_id};
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

  // Check session pin first
  if (!session_key.empty()) {
    auto pin_it = session_pins_.find(session_key);
    if (pin_it != session_pins_.end() &&
        pin_it->second.provider_id == provider_id) {
      const auto& pin = pin_it->second;
      auto key = cooldown_key(provider_id, pin.profile_id);
      if (!cooldown_.IsInCooldown(key)) {
        // Create provider with pinned profile's API key
        auto prof_it = profiles_.find(provider_id);
        if (prof_it != profiles_.end()) {
          for (const auto& profile : prof_it->second) {
            if (profile.id == pin.profile_id) {
              // Temporarily override the entry's API key
              auto provider =
                  registry_->GetProviderWithKey(provider_id, profile.api_key);
              if (provider) {
                return ResolvedProvider{provider, provider_id, pin.profile_id,
                                        ref.model, false};
              }
            }
          }
        }
        // Pin references a profile that no longer exists, try normal flow
      }
      // Pinned profile is in cooldown, clear pin
      session_pins_.erase(pin_it);
    }
  }

  // Check if this provider has auth profiles
  auto prof_it = profiles_.find(provider_id);
  if (prof_it != profiles_.end() && !prof_it->second.empty()) {
    // Try each profile in order
    for (const auto& profile : prof_it->second) {
      auto key = cooldown_key(provider_id, profile.id);
      if (cooldown_.IsInCooldown(key)) {
        continue;
      }

      auto provider =
          registry_->GetProviderWithKey(provider_id, profile.api_key);
      if (provider) {
        return ResolvedProvider{provider, provider_id, profile.id, ref.model,
                                false};
      }
    }

    // All profiles in cooldown — try probe throttling on the first profile.
    // If enough time has passed since the last probe, allow one attempt
    // so the system can detect recovery without waiting for full cooldown.
    {
      auto probe_key = cooldown_key(provider_id, prof_it->second[0].id);
      if (cooldown_.TryProbe(probe_key)) {
        logger_->info("Probing cooled-down profile {}:{} (probe throttle)",
                      provider_id, prof_it->second[0].id);
        auto provider = registry_->GetProviderWithKey(
            provider_id, prof_it->second[0].api_key);
        if (provider) {
          return ResolvedProvider{provider, provider_id, prof_it->second[0].id,
                                  ref.model, false};
        }
      }
    }

    logger_->debug("All profiles for provider '{}' are in cooldown",
                   provider_id);
    return std::nullopt;
  }

  // No profiles configured — use default provider entry
  auto key = cooldown_key(provider_id, "");
  if (cooldown_.IsInCooldown(key)) {
    // Probe throttling for default entry
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
