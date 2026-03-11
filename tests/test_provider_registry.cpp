// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/providers/provider_registry.hpp"

#include <gtest/gtest.h>

namespace quantclaw {

static std::shared_ptr<spdlog::logger> make_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

// --- ModelRef tests ---

TEST(ModelRefTest, ParseWithProvider) {
  auto ref = ModelRef::parse("anthropic/claude-opus-4-6");
  EXPECT_EQ(ref.provider, "anthropic");
  EXPECT_EQ(ref.model, "claude-opus-4-6");
}

TEST(ModelRefTest, ParseWithoutProvider) {
  auto ref = ModelRef::parse("gpt-4o", "openai");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-4o");
}

TEST(ModelRefTest, ToString) {
  ModelRef ref;
  ref.provider = "anthropic";
  ref.model = "claude-opus-4-6";
  EXPECT_EQ(ref.to_string(), "anthropic/claude-opus-4-6");
}

TEST(ModelRefTest, ParseWithDefaultProvider) {
  auto ref = ModelRef::parse("qwen-max", "qwen");
  EXPECT_EQ(ref.provider, "qwen");
  EXPECT_EQ(ref.model, "qwen-max");
}

// --- ProviderRegistry tests ---

TEST(ProviderRegistryTest, RegisterBuiltinFactories) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  // Should have factories but no entries yet
  EXPECT_FALSE(reg->HasProvider("nonexistent"));
}

TEST(ProviderRegistryTest, AddProviderEntry) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  entry.base_url = "https://api.openai.com/v1";
  reg->AddProvider(entry);

  EXPECT_TRUE(reg->HasProvider("openai"));
  auto* e = reg->GetEntry("openai");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->api_key, "test-key");
}

TEST(ProviderRegistryTest, LoadFromConfig) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  nlohmann::json config = {
      {"openai",
       {{"apiKey", "sk-test"},
        {"baseUrl", "https://api.openai.com/v1"},
        {"timeout", 60}}},
      {"anthropic", {{"apiKey", "ak-test"}, {"timeout", 45}}},
  };
  reg->LoadFromConfig(config);

  EXPECT_TRUE(reg->HasProvider("openai"));
  EXPECT_TRUE(reg->HasProvider("anthropic"));

  auto ids = reg->ProviderIds();
  EXPECT_EQ(ids.size(), 2);
}

TEST(ProviderRegistryTest, ModelAliases) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  reg->AddAlias("opus", "anthropic/claude-opus-4-6");
  reg->AddAlias("gpt4", "openai/gpt-4o");

  auto ref = reg->ResolveModel("opus");
  EXPECT_EQ(ref.provider, "anthropic");
  EXPECT_EQ(ref.model, "claude-opus-4-6");

  ref = reg->ResolveModel("gpt4");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-4o");

  // Non-aliased should pass through
  ref = reg->ResolveModel("openai/gpt-3.5-turbo");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-3.5-turbo");
}

TEST(ProviderRegistryTest, LoadAliasesFromJson) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  nlohmann::json aliases = {
      {"anthropic/claude-opus-4-6", {{"alias", "opus"}}},
      {"openai/gpt-4o", {{"alias", "gpt4"}}},
  };
  reg->LoadAliases(aliases);

  auto all = reg->Aliases();
  EXPECT_EQ(all.size(), 2);

  auto ref = reg->ResolveModel("opus");
  EXPECT_EQ(ref.model, "claude-opus-4-6");
}

TEST(ProviderRegistryTest, GetProviderCreatesInstance) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  reg->AddProvider(entry);

  auto provider = reg->GetProvider("openai");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->GetProviderName(), "openai");

  // Second call returns same instance
  auto provider2 = reg->GetProvider("openai");
  EXPECT_EQ(provider.get(), provider2.get());
}

TEST(ProviderRegistryTest, GetProviderForModel) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->RegisterBuiltinFactories();

  ProviderEntry entry;
  entry.id = "anthropic";
  entry.api_key = "test-key";
  reg->AddProvider(entry);

  auto ref = ModelRef::parse("anthropic/claude-opus-4-6");
  auto provider = reg->GetProviderForModel(ref);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderRegistryTest, NullForUnknownProvider) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  auto provider = reg->GetProvider("nonexistent");
  EXPECT_EQ(provider, nullptr);
}

TEST(ProviderRegistryTest, ProviderEntryInspection) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  ProviderEntry entry;
  entry.id = "ollama";
  entry.base_url = "http://localhost:11434/v1";
  entry.display_name = "Local Ollama";
  reg->AddProvider(entry);

  auto* e = reg->GetEntry("ollama");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->display_name, "Local Ollama");
  EXPECT_EQ(e->base_url, "http://localhost:11434/v1");
}

}  // namespace quantclaw
