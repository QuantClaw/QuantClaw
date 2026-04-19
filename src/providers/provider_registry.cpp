// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/providers/provider_registry.hpp"

#include <algorithm>
#include <string_view>

#include "quantclaw/auth/github_copilot_auth.hpp"
#include "quantclaw/auth/openai_codex_auth.hpp"
#include "quantclaw/providers/anthropic_provider.hpp"
#include "quantclaw/providers/github_copilot_provider.hpp"
#include "quantclaw/providers/openai_codex_provider.hpp"
#include "quantclaw/providers/openai_provider.hpp"

namespace quantclaw {
namespace {

std::string NormalizeProviderApi(const ProviderEntry& entry,
                                 std::string_view fallback_api) {
  if (!entry.api.empty()) {
    return entry.api;
  }
  return std::string(fallback_api);
}

std::string ResolveFactoryIdForEntry(const ProviderEntry& entry) {
  if (!entry.api.empty()) {
    if (entry.api == "anthropic-messages") {
      return "anthropic";
    }
    if (entry.api == "google-generative-ai") {
      return "gemini";
    }
    if (entry.api == "openai-codex-responses") {
      return "openai-codex";
    }
    if (entry.api == "openai-completions" || entry.api == "openai-responses" ||
        entry.api == "azure-openai-responses") {
      return "openai";
    }
  }
  return entry.id;
}

std::shared_ptr<LLMProvider> CreateOpenAICompatibleProvider(
    const ProviderEntry& entry, std::shared_ptr<spdlog::logger> logger,
    std::string_view default_base_url, std::string_view default_api) {
  std::string url =
      entry.base_url.empty() ? std::string(default_base_url) : entry.base_url;
  return std::make_shared<OpenAIProvider>(
      entry.api_key, url, entry.timeout, std::move(logger), entry.id,
      NormalizeProviderApi(entry, default_api));
}

}  // namespace

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
    return CreateOpenAICompatibleProvider(entry, std::move(logger),
                                          "https://api.openai.com/v1",
                                          "openai-completions");
  });

  RegisterFactory("openai-codex", [](const ProviderEntry& entry,
                                     std::shared_ptr<spdlog::logger> logger) {
    std::string url = entry.base_url.empty() ? "https://chatgpt.com/backend-api"
                                             : entry.base_url;
    auto auth_client = std::make_shared<auth::OpenAICodexOAuthClient>(logger);
    auto token_source = std::make_shared<auth::OpenAICodexCredentialResolver>(
        auth::OpenAICodexAuthStore(), auth_client, logger);
    return std::make_shared<OpenAICodexProvider>(url, entry.timeout, logger,
                                                 token_source);
  });

  RegisterFactory("github-copilot", [](const ProviderEntry& entry,
                                       std::shared_ptr<spdlog::logger> logger) {
    auto token_client =
        std::make_shared<auth::GitHubCopilotTokenClient>(logger);
    std::string github_token =
        entry.extra.is_object()
            ? entry.extra.value("githubToken", std::string{})
            : std::string{};
    auto resolver = std::make_shared<auth::GitHubCopilotRuntimeResolver>(
        auth::GitHubCopilotAuthStore(), auth::GitHubCopilotTokenCache(),
        token_client, logger, [github_token]() { return github_token; });
    const int timeout = entry.timeout > 0 ? entry.timeout : 30;
    return std::make_shared<GitHubCopilotProvider>(timeout, logger, resolver);
  });

  // Anthropic
  RegisterFactory("anthropic", [](const ProviderEntry& entry,
                                  std::shared_ptr<spdlog::logger> logger) {
    std::string url =
        entry.base_url.empty() ? "https://api.anthropic.com" : entry.base_url;
    return std::make_shared<AnthropicProvider>(entry.api_key, url,
                                               entry.timeout, logger);
  });

  // Ollama (uses OpenAI-compatible API)
  RegisterFactory("ollama", [](const ProviderEntry& entry,
                               std::shared_ptr<spdlog::logger> logger) {
    return CreateOpenAICompatibleProvider(entry, std::move(logger),
                                          "http://localhost:11434/v1",
                                          "openai-completions");
  });

  // Gemini / Google (uses OpenAI-compatible API via base_url override)
  RegisterFactory("gemini", [](const ProviderEntry& entry,
                               std::shared_ptr<spdlog::logger> logger) {
    return CreateOpenAICompatibleProvider(
        entry, std::move(logger),
        "https://generativelanguage.googleapis.com/v1beta/openai",
        "google-generative-ai");
  });

  // Google alias
  RegisterFactory("google", factories_["gemini"]);

  // Bedrock (uses OpenAI-compatible gateway)
  RegisterFactory("bedrock", [](const ProviderEntry& entry,
                                std::shared_ptr<spdlog::logger> logger) {
    return CreateOpenAICompatibleProvider(entry, std::move(logger),
                                          "http://localhost:8080/v1",
                                          "openai-completions");
  });

  // OpenRouter
  RegisterFactory("openrouter", [](const ProviderEntry& entry,
                                   std::shared_ptr<spdlog::logger> logger) {
    return CreateOpenAICompatibleProvider(entry, std::move(logger),
                                          "https://openrouter.ai/api/v1",
                                          "openai-completions");
  });

  // Together
  RegisterFactory("together", [](const ProviderEntry& entry,
                                 std::shared_ptr<spdlog::logger> logger) {
    return CreateOpenAICompatibleProvider(entry, std::move(logger),
                                          "https://api.together.xyz/v1",
                                          "openai-completions");
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
  if (!providers_json.is_object())
    return;

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
    entry.api = val.value("api", std::string{});
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
  if (!aliases_json.is_object())
    return;

  for (auto& [model_ref, val] : aliases_json.items()) {
    if (val.is_object() && val.contains("alias")) {
      alias_map_[val["alias"].get<std::string>()] = model_ref;
    } else if (val.is_string()) {
      alias_map_[val.get<std::string>()] = model_ref;
    }
  }
}

ModelRef
ProviderRegistry::ResolveModel(const std::string& raw,
                               const std::string& default_provider) const {
  // Check alias first
  auto it = alias_map_.find(raw);
  if (it != alias_map_.end()) {
    return ModelRef::parse(it->second, default_provider);
  }
  return ModelRef::parse(raw, default_provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProvider(const std::string& provider_id) {
  // Return cached instance if available
  auto it = instances_.find(provider_id);
  if (it != instances_.end())
    return it->second;

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

  auto factory_id = ResolveFactoryIdForEntry(eit->second);
  auto fit = factories_.find(factory_id);
  if (fit == factories_.end()) {
    logger_->error(
        "No factory registered for provider: {} (resolved from {} with api={})",
        factory_id, provider_id, eit->second.api);
    return nullptr;
  }

  auto provider = fit->second(eit->second, logger_);
  instances_[provider_id] = provider;
  return provider;
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderForModel(const ModelRef& ref) {
  return GetProvider(ref.provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderWithKey(const std::string& provider_id,
                                     const std::string& api_key) {
  // Build a temporary entry with the given API key
  ProviderEntry entry;
  auto eit = entries_.find(provider_id);
  if (eit != entries_.end()) {
    entry = eit->second;
  } else {
    entry.id = provider_id;
  }
  entry.api_key = api_key;

  auto factory_id = ResolveFactoryIdForEntry(entry);
  auto fit = factories_.find(factory_id);
  if (fit == factories_.end()) {
    logger_->error("No factory for provider: {} (resolved from {} with api={})",
                   factory_id, provider_id, entry.api);
    return nullptr;
  }

  return fit->second(entry, logger_);
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
  if (entries_.count(provider_id) > 0) {
    const auto& entry = entries_.at(provider_id);
    return factories_.count(ResolveFactoryIdForEntry(entry)) > 0;
  }
  return factories_.count(provider_id) > 0;
}

const ProviderEntry*
ProviderRegistry::GetEntry(const std::string& provider_id) const {
  auto it = entries_.find(provider_id);
  return it != entries_.end() ? &it->second : nullptr;
}

void ProviderRegistry::LoadModelProviders(
    const std::unordered_map<std::string, ProviderConfig>& model_providers) {
  for (const auto& [id, prov] : model_providers) {
    auto it = entries_.find(id);
    if (it != entries_.end()) {
      // Merge models into existing entry
      for (const auto& m : prov.models) {
        it->second.models.push_back(m);
      }
      if (!prov.api.empty()) {
        it->second.api = prov.api;
      }
    } else {
      // Create new entry
      ProviderEntry entry;
      entry.id = id;
      entry.display_name = id;
      entry.api_key = prov.api_key;
      entry.base_url = prov.base_url;
      entry.api = prov.api;
      entry.timeout = prov.timeout;
      entry.models = prov.models;

      // Resolve API key from env if needed
      if (entry.api_key.empty()) {
        entry.api_key = resolve_api_key(entry);
      }

      entries_[id] = entry;
    }
    logger_->debug("Loaded model provider: {} ({} models)", id,
                   prov.models.size());
  }
}

nlohmann::json ProviderRegistry::ModelCatalogEntry::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  j["name"] = name;
  j["provider"] = provider;
  if (context_window > 0)
    j["contextWindow"] = context_window;
  j["reasoning"] = reasoning;
  if (!input.empty())
    j["input"] = input;
  if (max_tokens > 0)
    j["maxTokens"] = max_tokens;
  if (cost.input > 0 || cost.output > 0) {
    j["cost"] = {{"input", cost.input}, {"output", cost.output}};
    if (cost.cache_read > 0)
      j["cost"]["cacheRead"] = cost.cache_read;
    if (cost.cache_write > 0)
      j["cost"]["cacheWrite"] = cost.cache_write;
  }
  return j;
}

std::vector<ProviderRegistry::ModelCatalogEntry>
ProviderRegistry::GetModelCatalog() const {
  std::vector<ModelCatalogEntry> catalog;
  for (const auto& [pid, entry] : entries_) {
    for (const auto& m : entry.models) {
      ModelCatalogEntry ce;
      ce.id = m.id;
      ce.name = m.name;
      ce.provider = pid;
      ce.context_window = m.context_window;
      ce.reasoning = m.reasoning;
      ce.input = m.input;
      ce.cost = m.cost;
      ce.max_tokens = m.max_tokens;
      catalog.push_back(std::move(ce));
    }
  }
  return catalog;
}

std::string
ProviderRegistry::resolve_api_key(const ProviderEntry& entry) const {
  // Direct value
  if (!entry.api_key.empty())
    return entry.api_key;

  // Explicit env var
  if (!entry.api_key_env.empty()) {
    const char* val = std::getenv(entry.api_key_env.c_str());
    if (val)
      return val;
  }

  // Convention-based env vars
  std::string upper_id = entry.id;
  std::transform(upper_id.begin(), upper_id.end(), upper_id.begin(), ::toupper);

  // Try PROVIDER_API_KEY (e.g. OPENAI_API_KEY)
  std::string env_name = upper_id + "_API_KEY";
  const char* val = std::getenv(env_name.c_str());
  if (val)
    return val;

  // Try PROVIDER_KEY
  env_name = upper_id + "_KEY";
  val = std::getenv(env_name.c_str());
  if (val)
    return val;

  return "";
}

}  // namespace quantclaw
