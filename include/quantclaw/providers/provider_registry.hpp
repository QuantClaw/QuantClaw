// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/providers/llm_provider.hpp"

namespace quantclaw {

// Model reference: "provider/model" format (compatible with OpenClaw)
struct ModelRef {
  std::string provider;  // e.g. "anthropic", "openai", "ollama"
  std::string model;     // e.g. "claude-opus-4-6", "gpt-4o"

  std::string to_string() const {
    return provider + "/" + model;
  }

  static ModelRef parse(const std::string& raw,
                        const std::string& default_provider = "openai");
};

// Provider entry with auth and config
struct ProviderEntry {
  std::string id;  // e.g. "anthropic", "openai"
  std::string display_name;
  std::string base_url;
  std::string api_key;
  std::string api_key_env;  // Env var name for API key
  std::string api;  // API type: "openai-completions", "anthropic-messages"
  int timeout = 30;
  nlohmann::json extra;                 // Provider-specific settings
  std::vector<ModelDefinition> models;  // Per-provider model definitions
};

// Model alias mapping (OpenClaw compatible)
struct ModelAlias {
  std::string alias;   // Short name (e.g. "opus")
  std::string target;  // Full model ref (e.g. "anthropic/claude-opus-4-6")
};

// Provider factory: creates LLMProvider instances
using ProviderFactory = std::function<std::shared_ptr<LLMProvider>(
    const ProviderEntry& entry, std::shared_ptr<spdlog::logger> logger)>;

// Central provider registry — manages all LLM providers and model resolution
class ProviderRegistry {
 public:
  explicit ProviderRegistry(std::shared_ptr<spdlog::logger> logger);

  // Register a provider factory (e.g. "openai", "anthropic", "ollama")
  void RegisterFactory(const std::string& provider_id, ProviderFactory factory);

  // Register built-in provider factories (openai, anthropic, ollama, gemini)
  void RegisterBuiltinFactories();

  // Add a provider entry (from config)
  void AddProvider(const ProviderEntry& entry);

  // Add a model alias
  void AddAlias(const std::string& alias, const std::string& target);

  // Load providers from config JSON
  void LoadFromConfig(const nlohmann::json& providers_json);

  // Load model aliases from config JSON
  void LoadAliases(const nlohmann::json& aliases_json);

  // Resolve a model string to a full ModelRef, expanding aliases
  ModelRef ResolveModel(const std::string& raw,
                        const std::string& default_provider = "openai") const;

  // Get or create a provider instance for a given provider ID
  std::shared_ptr<LLMProvider> GetProvider(const std::string& provider_id);

  // Get or create a provider for a model ref
  std::shared_ptr<LLMProvider> GetProviderForModel(const ModelRef& ref);

  // Create a provider instance using a specific API key
  // (for multi-profile auth rotation). Not cached.
  std::shared_ptr<LLMProvider>
  GetProviderWithKey(const std::string& provider_id,
                     const std::string& api_key);

  // List all registered provider IDs
  std::vector<std::string> ProviderIds() const;

  // List all registered aliases
  std::vector<ModelAlias> Aliases() const;

  // Check if a provider is available (has factory + entry)
  bool HasProvider(const std::string& provider_id) const;

  // Get provider entry (for inspection)
  const ProviderEntry* GetEntry(const std::string& provider_id) const;

  // Load models from model_providers config section (OpenClaw models.providers)
  void LoadModelProviders(
      const std::unordered_map<std::string, ProviderConfig>& model_providers);

  // Model catalog entry (merged from all providers)
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

  // Get model catalog (merged from all providers)
  std::vector<ModelCatalogEntry> GetModelCatalog() const;

 private:
  std::shared_ptr<spdlog::logger> logger_;

  std::unordered_map<std::string, ProviderFactory> factories_;
  std::unordered_map<std::string, ProviderEntry> entries_;
  std::unordered_map<std::string, std::shared_ptr<LLMProvider>> instances_;
  std::unordered_map<std::string, std::string> alias_map_;  // alias → target

  // Resolve API key from entry (direct value or env var)
  std::string resolve_api_key(const ProviderEntry& entry) const;
};

}  // namespace quantclaw
