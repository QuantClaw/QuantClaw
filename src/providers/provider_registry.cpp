// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/provider_registry.hpp"
#include "quantclaw/providers/openai_provider.hpp"
#include "quantclaw/providers/anthropic_provider.hpp"

#include <cstdlib>
#include <algorithm>

namespace quantclaw {

// --- ModelRef ---

ModelRef ModelRef::parse(const std::string& raw,
                         const std::string& default_provider) {
  ModelRef ref;
  auto slash = raw.find('/');
  if (slash != std::string::npos) {
    ref.provider = raw.substr(0, slash);
    ref.model = raw.substr(slash + 1);
  } else {
    ref.provider = default_provider;
    ref.model = raw;
  }
  return ref;
}

// --- ProviderRegistry ---

ProviderRegistry::ProviderRegistry(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void ProviderRegistry::RegisterFactory(const std::string& provider_id,
                                         ProviderFactory factory) {
  factories_[provider_id] = std::move(factory);
}

void ProviderRegistry::RegisterBuiltinFactories() {
  // OpenAI-compatible factory (also works for Ollama, Together, etc.)
  RegisterFactory("openai", [](const ProviderEntry& entry,
                                 std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "https://api.openai.com/v1"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // Anthropic
  RegisterFactory("anthropic", [](const ProviderEntry& entry,
                                    std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "https://api.anthropic.com"
                          : entry.base_url;
    return std::make_shared<AnthropicProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // Ollama (uses OpenAI-compatible API)
  RegisterFactory("ollama", [](const ProviderEntry& entry,
                                 std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "http://localhost:11434/v1"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // Gemini / Google (uses OpenAI-compatible API via base_url override)
  RegisterFactory("gemini", [](const ProviderEntry& entry,
                                 std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "https://generativelanguage.googleapis.com/v1beta/openai"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // Google alias
  RegisterFactory("google", factories_["gemini"]);

  // Bedrock (uses OpenAI-compatible gateway)
  RegisterFactory("bedrock", [](const ProviderEntry& entry,
                                  std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "http://localhost:8080/v1"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // OpenRouter
  RegisterFactory("openrouter", [](const ProviderEntry& entry,
                                     std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "https://openrouter.ai/api/v1"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });

  // Together
  RegisterFactory("together", [](const ProviderEntry& entry,
                                   std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty()
                          ? "https://api.together.xyz/v1"
                          : entry.base_url;
    return std::make_shared<OpenAIProvider>(
        entry.api_key, url, entry.timeout, logger);
  });
}

void ProviderRegistry::AddProvider(const ProviderEntry& entry) {
  entries_[entry.id] = entry;
}

void ProviderRegistry::AddAlias(const std::string& alias,
                                  const std::string& target) {
  alias_map_[alias] = target;
}

void ProviderRegistry::LoadFromConfig(const nlohmann::json& providers_json) {
  if (!providers_json.is_object()) return;

  for (auto& [id, val] : providers_json.items()) {
    ProviderEntry entry;
    entry.id = id;
    entry.display_name = val.value("displayName", id);
    entry.base_url = val.value("baseUrl", std::string{});
    if (entry.base_url.empty()) {
      entry.base_url = val.value("base_url", std::string{});
    }
    entry.api_key = val.value("apiKey", std::string{});
    if (entry.api_key.empty()) {
      entry.api_key = val.value("api_key", std::string{});
    }
    entry.api_key_env = val.value("apiKeyEnv", std::string{});
    if (entry.api_key_env.empty()) {
      entry.api_key_env = val.value("api_key_env", std::string{});
    }
    entry.timeout = val.value("timeout", 30);
    if (val.contains("extra")) {
      entry.extra = val["extra"];
    }

    // Resolve API key from env if needed
    if (entry.api_key.empty()) {
      entry.api_key = resolve_api_key(entry);
    }

    entries_[id] = entry;
    logger_->debug("Loaded provider: {}", id);
  }
}

void ProviderRegistry::LoadAliases(const nlohmann::json& aliases_json) {
  if (!aliases_json.is_object()) return;

  for (auto& [model_ref, val] : aliases_json.items()) {
    if (val.is_object() && val.contains("alias")) {
      alias_map_[val["alias"].get<std::string>()] = model_ref;
    } else if (val.is_string()) {
      alias_map_[val.get<std::string>()] = model_ref;
    }
  }
}

ModelRef ProviderRegistry::ResolveModel(
    const std::string& raw,
    const std::string& default_provider) const {
  // Check alias first
  auto it = alias_map_.find(raw);
  if (it != alias_map_.end()) {
    return ModelRef::parse(it->second, default_provider);
  }
  return ModelRef::parse(raw, default_provider);
}

std::shared_ptr<LLMProvider> ProviderRegistry::GetProvider(
    const std::string& provider_id) {
  // Return cached instance if available
  auto it = instances_.find(provider_id);
  if (it != instances_.end()) return it->second;

  // Find factory
  auto fit = factories_.find(provider_id);
  if (fit == factories_.end()) {
    logger_->error("No factory registered for provider: {}", provider_id);
    return nullptr;
  }

  // Find entry
  auto eit = entries_.find(provider_id);
  if (eit == entries_.end()) {
    // Create minimal entry with env-based defaults
    ProviderEntry entry;
    entry.id = provider_id;
    entry.api_key = resolve_api_key(entry);
    entries_[provider_id] = entry;
    eit = entries_.find(provider_id);
  }

  auto provider = fit->second(eit->second, logger_);
  instances_[provider_id] = provider;
  return provider;
}

std::shared_ptr<LLMProvider> ProviderRegistry::GetProviderForModel(
    const ModelRef& ref) {
  return GetProvider(ref.provider);
}

std::vector<std::string> ProviderRegistry::ProviderIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, _] : entries_) {
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<ModelAlias> ProviderRegistry::Aliases() const {
  std::vector<ModelAlias> result;
  for (const auto& [alias, target] : alias_map_) {
    result.push_back({alias, target});
  }
  return result;
}

bool ProviderRegistry::HasProvider(const std::string& provider_id) const {
  return factories_.count(provider_id) > 0 || entries_.count(provider_id) > 0;
}

const ProviderEntry* ProviderRegistry::GetEntry(
    const std::string& provider_id) const {
  auto it = entries_.find(provider_id);
  return it != entries_.end() ? &it->second : nullptr;
}

std::string ProviderRegistry::resolve_api_key(
    const ProviderEntry& entry) const {
  // Direct value
  if (!entry.api_key.empty()) return entry.api_key;

  // Explicit env var
  if (!entry.api_key_env.empty()) {
    const char* val = std::getenv(entry.api_key_env.c_str());
    if (val) return val;
  }

  // Convention-based env vars
  std::string upper_id = entry.id;
  std::transform(upper_id.begin(), upper_id.end(), upper_id.begin(), ::toupper);

  // Try PROVIDER_API_KEY (e.g. OPENAI_API_KEY)
  std::string env_name = upper_id + "_API_KEY";
  const char* val = std::getenv(env_name.c_str());
  if (val) return val;

  // Try PROVIDER_KEY
  env_name = upper_id + "_KEY";
  val = std::getenv(env_name.c_str());
  if (val) return val;

  return "";
}

}  // namespace quantclaw
