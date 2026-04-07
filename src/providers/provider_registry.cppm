// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.providers.provider_registry;

import std;
import nlohmann.json;
import quantclaw.config;
import quantclaw.providers.curl_raii;
import quantclaw.providers.anthropic_provider;
import quantclaw.providers.llm_provider;

export namespace quantclaw {

struct ModelRef {
  std::string provider;
  std::string model;

  std::string to_string() const {
    return provider + "/" + model;
  }

  static ModelRef parse(const std::string& raw,
                        const std::string& default_provider = "local");
};

struct ProviderEntry {
  std::string id;
  std::string display_name;
  std::string base_url;
  std::string api_key;
  std::string api_key_env;
  std::string api;
  int timeout = 30;
  nlohmann::json extra;
  std::vector<ModelDefinition> models;
};

struct ModelAlias {
  std::string alias;
  std::string target;
};

using ProviderFactory = std::function<std::shared_ptr<LLMProvider>(
    const ProviderEntry& entry, std::shared_ptr<spdlog::logger> logger)>;

class ProviderRegistry {
 public:
  explicit ProviderRegistry(std::shared_ptr<spdlog::logger> logger);

  void RegisterFactory(const std::string& provider_id, ProviderFactory factory);
  void RegisterBuiltinFactories();
  void AddProvider(const ProviderEntry& entry);
  void AddAlias(const std::string& alias, const std::string& target);
  void LoadFromConfig(const nlohmann::json& providers_json);
  void LoadAliases(const nlohmann::json& aliases_json);
  ModelRef ResolveModel(const std::string& raw,
                        const std::string& default_provider = "local") const;
  std::shared_ptr<LLMProvider> GetProvider(const std::string& provider_id);
  std::shared_ptr<LLMProvider> GetProviderForModel(const ModelRef& ref);
  std::shared_ptr<LLMProvider>
  GetProviderWithKey(const std::string& provider_id,
                     const std::string& api_key);
  std::vector<std::string> ProviderIds() const;
  std::vector<ModelAlias> Aliases() const;
  bool HasProvider(const std::string& provider_id) const;
  const ProviderEntry* GetEntry(const std::string& provider_id) const;
  void LoadModelProviders(
      const std::unordered_map<std::string, ProviderConfig>& model_providers);

  struct ModelCatalogEntry {
    std::string id;
    std::string name;
    std::string provider;
    int context_window = 0;
    bool reasoning = false;
    std::vector<std::string> input;
    ModelCost cost;
    int max_tokens = 0;
    nlohmann::json ToJson() const;
  };

  std::vector<ModelCatalogEntry> GetModelCatalog() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;

  std::unordered_map<std::string, ProviderFactory> factories_;
  std::unordered_map<std::string, ProviderEntry> entries_;
  std::unordered_map<std::string, std::shared_ptr<LLMProvider>> instances_;
  std::unordered_map<std::string, std::string> alias_map_;

  std::string resolve_api_key(const ProviderEntry& entry) const;
};

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

ProviderRegistry::ProviderRegistry(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

void ProviderRegistry::RegisterFactory(const std::string& provider_id,
                                       ProviderFactory factory) {
  factories_[provider_id] = std::move(factory);
}

void ProviderRegistry::RegisterBuiltinFactories() {
  RegisterFactory("local", [](const ProviderEntry& entry,
                               std::shared_ptr<spdlog::logger> logger) {
    std::string url =
        entry.base_url.empty() ? "http://127.0.0.1:8081" : entry.base_url;
    std::string api_key = entry.api_key.empty() ? "local" : entry.api_key;
    return std::make_shared<AnthropicProvider>(api_key, url, entry.timeout,
                                               logger);
  });

  RegisterFactory("anthropic", [](const ProviderEntry& entry,
                                   std::shared_ptr<spdlog::logger> logger) {
    std::string url =
        entry.base_url.empty() ? "https://api.anthropic.com" : entry.base_url;
    return std::make_shared<AnthropicProvider>(entry.api_key, url,
                                               entry.timeout, logger);
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

  for (auto it = providers_json.begin(); it != providers_json.end(); ++it) {
    const std::string id = it.key();
    const auto& val = it.value();
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

  for (auto it = aliases_json.begin(); it != aliases_json.end(); ++it) {
    const std::string model_ref = it.key();
    const auto& val = it.value();
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
  auto it = alias_map_.find(raw);
  if (it != alias_map_.end()) {
    return ModelRef::parse(it->second, default_provider);
  }
  return ModelRef::parse(raw, default_provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProvider(const std::string& provider_id) {
  auto it = instances_.find(provider_id);
  if (it != instances_.end())
    return it->second;

  auto fit = factories_.find(provider_id);
  if (fit == factories_.end()) {
    logger_->error("No factory registered for provider: {}", provider_id);
    return nullptr;
  }

  auto eit = entries_.find(provider_id);
  if (eit == entries_.end()) {
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

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderForModel(const ModelRef& ref) {
  return GetProvider(ref.provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderWithKey(const std::string& provider_id,
                                     const std::string& api_key) {
  auto fit = factories_.find(provider_id);
  if (fit == factories_.end()) {
    logger_->error("No factory for provider: {}", provider_id);
    return nullptr;
  }

  ProviderEntry entry;
  auto eit = entries_.find(provider_id);
  if (eit != entries_.end()) {
    entry = eit->second;
  } else {
    entry.id = provider_id;
  }
  entry.api_key = api_key;

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
  return factories_.count(provider_id) > 0 || entries_.count(provider_id) > 0;
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
      for (const auto& m : prov.models) {
        it->second.models.push_back(m);
      }
      if (!prov.api.empty()) {
        it->second.api = prov.api;
      }
    } else {
      ProviderEntry entry;
      entry.id = id;
      entry.display_name = id;
      entry.api_key = prov.api_key;
      entry.base_url = prov.base_url;
      entry.api = prov.api;
      entry.timeout = prov.timeout;
      entry.models = prov.models;

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

}  // namespace quantclaw