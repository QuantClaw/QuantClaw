// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.providers.provider_registry;

import std;
import nlohmann.json;
import "quantclaw/config.hpp";
import quantclaw.providers.llm_provider;

namespace spdlog {
class logger;
}

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

}  // namespace quantclaw