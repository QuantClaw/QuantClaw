#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "quantclaw/providers/provider_registry.hpp"

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
  reg->register_builtin_factories();

  // Should have factories but no entries yet
  EXPECT_FALSE(reg->has_provider("nonexistent"));
}

TEST(ProviderRegistryTest, AddProviderEntry) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->register_builtin_factories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  entry.base_url = "https://api.openai.com/v1";
  reg->add_provider(entry);

  EXPECT_TRUE(reg->has_provider("openai"));
  auto* e = reg->get_entry("openai");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->api_key, "test-key");
}

TEST(ProviderRegistryTest, LoadFromConfig) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->register_builtin_factories();

  nlohmann::json config = {
      {"openai",
       {{"apiKey", "sk-test"},
        {"baseUrl", "https://api.openai.com/v1"},
        {"timeout", 60}}},
      {"anthropic",
       {{"apiKey", "ak-test"},
        {"timeout", 45}}},
  };
  reg->load_from_config(config);

  EXPECT_TRUE(reg->has_provider("openai"));
  EXPECT_TRUE(reg->has_provider("anthropic"));

  auto ids = reg->provider_ids();
  EXPECT_EQ(ids.size(), 2);
}

TEST(ProviderRegistryTest, ModelAliases) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  reg->add_alias("opus", "anthropic/claude-opus-4-6");
  reg->add_alias("gpt4", "openai/gpt-4o");

  auto ref = reg->resolve_model("opus");
  EXPECT_EQ(ref.provider, "anthropic");
  EXPECT_EQ(ref.model, "claude-opus-4-6");

  ref = reg->resolve_model("gpt4");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-4o");

  // Non-aliased should pass through
  ref = reg->resolve_model("openai/gpt-3.5-turbo");
  EXPECT_EQ(ref.provider, "openai");
  EXPECT_EQ(ref.model, "gpt-3.5-turbo");
}

TEST(ProviderRegistryTest, LoadAliasesFromJson) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  nlohmann::json aliases = {
      {"anthropic/claude-opus-4-6", {{"alias", "opus"}}},
      {"openai/gpt-4o", {{"alias", "gpt4"}}},
  };
  reg->load_aliases(aliases);

  auto all = reg->aliases();
  EXPECT_EQ(all.size(), 2);

  auto ref = reg->resolve_model("opus");
  EXPECT_EQ(ref.model, "claude-opus-4-6");
}

TEST(ProviderRegistryTest, GetProviderCreatesInstance) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->register_builtin_factories();

  ProviderEntry entry;
  entry.id = "openai";
  entry.api_key = "test-key";
  reg->add_provider(entry);

  auto provider = reg->get_provider("openai");
  ASSERT_NE(provider, nullptr);
  EXPECT_EQ(provider->get_provider_name(), "openai");

  // Second call returns same instance
  auto provider2 = reg->get_provider("openai");
  EXPECT_EQ(provider.get(), provider2.get());
}

TEST(ProviderRegistryTest, GetProviderForModel) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  reg->register_builtin_factories();

  ProviderEntry entry;
  entry.id = "anthropic";
  entry.api_key = "test-key";
  reg->add_provider(entry);

  auto ref = ModelRef::parse("anthropic/claude-opus-4-6");
  auto provider = reg->get_provider_for_model(ref);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderRegistryTest, NullForUnknownProvider) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));
  auto provider = reg->get_provider("nonexistent");
  EXPECT_EQ(provider, nullptr);
}

TEST(ProviderRegistryTest, ProviderEntryInspection) {
  auto reg = std::make_unique<ProviderRegistry>(make_logger("providers"));

  ProviderEntry entry;
  entry.id = "ollama";
  entry.base_url = "http://localhost:11434/v1";
  entry.display_name = "Local Ollama";
  reg->add_provider(entry);

  auto* e = reg->get_entry("ollama");
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->display_name, "Local Ollama");
  EXPECT_EQ(e->base_url, "http://localhost:11434/v1");
}

}  // namespace quantclaw
