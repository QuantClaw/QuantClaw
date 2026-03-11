// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <filesystem>
#include <fstream>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/plugins/hook_manager.hpp"
#include "quantclaw/plugins/plugin_manifest.hpp"
#include "quantclaw/plugins/plugin_registry.hpp"
#include "quantclaw/plugins/plugin_system.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

namespace fs = std::filesystem;

static std::shared_ptr<spdlog::logger>
make_null_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

class PluginManifestTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_test_plugins");
  }

  void TearDown() override {
    fs::remove_all(test_dir_);
  }

  fs::path write_manifest(const std::string& plugin_name,
                          const nlohmann::json& manifest) {
    auto plugin_dir = test_dir_ / plugin_name;
    fs::create_directories(plugin_dir);
    auto manifest_path = plugin_dir / "openclaw.plugin.json";
    std::ofstream ofs(manifest_path);
    ofs << manifest.dump(2);
    return manifest_path;
  }

  fs::path test_dir_;
};

// --- PluginManifest Tests ---

TEST_F(PluginManifestTest, ParseMinimalManifest) {
  nlohmann::json j = {{"id", "test-plugin"}};
  auto m = quantclaw::PluginManifest::Parse(j);
  EXPECT_EQ(m.id, "test-plugin");
  EXPECT_EQ(m.name, "test-plugin");  // defaults to id
  EXPECT_TRUE(m.description.empty());
  EXPECT_TRUE(m.version.empty());
  EXPECT_TRUE(m.kind.empty());
  EXPECT_TRUE(m.channels.empty());
  EXPECT_TRUE(m.providers.empty());
  EXPECT_TRUE(m.skills.empty());
}

TEST_F(PluginManifestTest, ParseFullManifest) {
  nlohmann::json j = {
      {"id", "discord"},
      {"name", "Discord Channel"},
      {"description", "Discord integration"},
      {"version", "1.2.3"},
      {"kind", "memory"},
      {"channels", {"discord"}},
      {"providers", {"discord-bot"}},
      {"skills", {"discord-status", "discord-send"}},
      {"configSchema", {{"type", "object"}}},
  };
  auto m = quantclaw::PluginManifest::Parse(j);
  EXPECT_EQ(m.id, "discord");
  EXPECT_EQ(m.name, "Discord Channel");
  EXPECT_EQ(m.description, "Discord integration");
  EXPECT_EQ(m.version, "1.2.3");
  EXPECT_EQ(m.kind, "memory");
  ASSERT_EQ(m.channels.size(), 1);
  EXPECT_EQ(m.channels[0], "discord");
  ASSERT_EQ(m.providers.size(), 1);
  EXPECT_EQ(m.providers[0], "discord-bot");
  ASSERT_EQ(m.skills.size(), 2);
  EXPECT_EQ(m.config_schema["type"], "object");
}

TEST_F(PluginManifestTest, ParseMissingIdThrows) {
  nlohmann::json j = {{"name", "no-id"}};
  EXPECT_THROW(quantclaw::PluginManifest::Parse(j), std::runtime_error);
}

TEST_F(PluginManifestTest, LoadFromFile) {
  nlohmann::json manifest = {
      {"id", "file-test"},
      {"name", "File Test Plugin"},
      {"version", "0.1.0"},
  };
  auto path = write_manifest("file-test", manifest);
  auto m = quantclaw::PluginManifest::LoadFromFile(path);
  EXPECT_EQ(m.id, "file-test");
  EXPECT_EQ(m.name, "File Test Plugin");
}

TEST_F(PluginManifestTest, LoadFromNonexistentFileThrows) {
  EXPECT_THROW(
      quantclaw::PluginManifest::LoadFromFile("/nonexistent/path.json"),
      std::runtime_error);
}

TEST_F(PluginManifestTest, ToJsonRoundTrip) {
  nlohmann::json j = {
      {"id", "roundtrip"},
      {"name", "Roundtrip Test"},
      {"description", "Testing round-trip"},
      {"version", "2.0.0"},
      {"channels", {"ch1", "ch2"}},
      {"configSchema", {{"type", "object"}}},
  };
  auto m = quantclaw::PluginManifest::Parse(j);
  auto out = m.ToJson();
  EXPECT_EQ(out["id"], "roundtrip");
  EXPECT_EQ(out["name"], "Roundtrip Test");
  EXPECT_EQ(out["channels"].size(), 2);
}

TEST_F(PluginManifestTest, ParseUiHints) {
  nlohmann::json j = {
      {"id", "hints"},
      {"uiHints",
       {
           {"apiKey",
            {
                {"label", "API Key"},
                {"help", "Your secret key"},
                {"sensitive", true},
                {"advanced", false},
                {"tags", {"auth", "security"}},
            }},
       }},
  };
  auto m = quantclaw::PluginManifest::Parse(j);
  ASSERT_EQ(m.ui_hints.size(), 1);
  ASSERT_TRUE(m.ui_hints.count("apiKey"));
  EXPECT_EQ(m.ui_hints.at("apiKey").label, "API Key");
  EXPECT_TRUE(m.ui_hints.at("apiKey").sensitive);
  EXPECT_EQ(m.ui_hints.at("apiKey").tags.size(), 2);
}

// --- PluginRegistry Tests ---

class PluginRegistryTest : public PluginManifestTest {
 protected:
  void SetUp() override {
    PluginManifestTest::SetUp();
    logger_ = make_null_logger("plugin_reg_test");
  }

  void TearDown() override {
    PluginManifestTest::TearDown();
  }

  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(PluginRegistryTest, DiscoverEmptyDirectory) {
  quantclaw::PluginRegistry reg(logger_);
  quantclaw::QuantClawConfig config;
  reg.Discover(config, test_dir_);
  EXPECT_TRUE(reg.Plugins().empty());
}

TEST_F(PluginRegistryTest, DiscoverPluginsFromConfigPaths) {
  // Create plugin directories with manifests
  auto plugins_dir = test_dir_ / "my-plugins";
  fs::create_directories(plugins_dir / "plugin-a");
  fs::create_directories(plugins_dir / "plugin-b");

  {
    std::ofstream ofs(plugins_dir / "plugin-a" / "openclaw.plugin.json");
    ofs << R"({"id": "plugin-a", "name": "Plugin A"})";
  }
  {
    std::ofstream ofs(plugins_dir / "plugin-b" / "openclaw.plugin.json");
    ofs << R"({"id": "plugin-b", "channels": ["telegram"]})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {
      {"load", {{"paths", {plugins_dir.string()}}}},
  };

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  EXPECT_EQ(reg.Plugins().size(), 2);
  EXPECT_TRUE(reg.Find("plugin-a") != nullptr);
  EXPECT_TRUE(reg.Find("plugin-b") != nullptr);
  EXPECT_EQ(reg.Find("plugin-a")->name, "Plugin A");
  ASSERT_EQ(reg.Find("plugin-b")->channel_ids.size(), 1);
  EXPECT_EQ(reg.Find("plugin-b")->channel_ids[0], "telegram");
}

TEST_F(PluginRegistryTest, EnableDisableLogic) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "enabled-one");
  fs::create_directories(plugins_dir / "disabled-one");

  {
    std::ofstream ofs(plugins_dir / "enabled-one" / "openclaw.plugin.json");
    ofs << R"({"id": "enabled-one"})";
  }
  {
    std::ofstream ofs(plugins_dir / "disabled-one" / "openclaw.plugin.json");
    ofs << R"({"id": "disabled-one"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {
      {"load", {{"paths", {plugins_dir.string()}}}},
      {"deny", {"disabled-one"}},
  };

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  EXPECT_TRUE(reg.IsEnabled("enabled-one"));
  EXPECT_FALSE(reg.IsEnabled("disabled-one"));
}

TEST_F(PluginRegistryTest, AllowListRestrictsPlugins) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "allowed");
  fs::create_directories(plugins_dir / "not-allowed");

  {
    std::ofstream ofs(plugins_dir / "allowed" / "openclaw.plugin.json");
    ofs << R"({"id": "allowed"})";
  }
  {
    std::ofstream ofs(plugins_dir / "not-allowed" / "openclaw.plugin.json");
    ofs << R"({"id": "not-allowed"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {
      {"load", {{"paths", {plugins_dir.string()}}}},
      {"allow", {"allowed"}},
  };

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  EXPECT_TRUE(reg.IsEnabled("allowed"));
  EXPECT_FALSE(reg.IsEnabled("not-allowed"));
}

TEST_F(PluginRegistryTest, ToJsonIncludesAllFields) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "json-test");
  {
    std::ofstream ofs(plugins_dir / "json-test" / "openclaw.plugin.json");
    ofs << R"({"id":"json-test","name":"JSON Test","version":"1.0","channels":["ch"]})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  auto j = reg.ToJson();
  ASSERT_EQ(j.size(), 1);
  EXPECT_EQ(j[0]["id"], "json-test");
  EXPECT_EQ(j[0]["name"], "JSON Test");
  EXPECT_EQ(j[0]["version"], "1.0");
  EXPECT_EQ(j[0]["status"], "loaded");
}

TEST_F(PluginRegistryTest, QuantclawManifestAlsoDiscovered) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "qc-plugin");
  {
    std::ofstream ofs(plugins_dir / "qc-plugin" / "quantclaw.plugin.json");
    ofs << R"({"id": "qc-plugin"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  EXPECT_TRUE(reg.Find("qc-plugin") != nullptr);
}

TEST_F(PluginRegistryTest, GlobalDisable) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "some-plugin");
  {
    std::ofstream ofs(plugins_dir / "some-plugin" / "openclaw.plugin.json");
    ofs << R"({"id": "some-plugin"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {
      {"enabled", false},
      {"load", {{"paths", {plugins_dir.string()}}}},
  };

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  EXPECT_FALSE(reg.IsEnabled("some-plugin"));
}

TEST_F(PluginRegistryTest, PluginConfigPassthrough) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "cfg-plugin");
  {
    std::ofstream ofs(plugins_dir / "cfg-plugin" / "openclaw.plugin.json");
    ofs << R"({"id": "cfg-plugin"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {
      {"load", {{"paths", {plugins_dir.string()}}}},
      {"entries", {{"cfg-plugin", {{"config", {{"key", "value"}}}}}}},
  };

  quantclaw::PluginRegistry reg(logger_);
  reg.Discover(config, test_dir_);

  auto* rec = reg.Find("cfg-plugin");
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->plugin_config["key"], "value");
}

// --- HookManager Tests ---

class HookManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    logger_ = make_null_logger("hook_test");
  }

  void TearDown() override {
    // no-op
  }

  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(HookManagerTest, RegisterAndFire) {
  quantclaw::HookManager hooks(logger_);

  bool called = false;
  // Use a modifying hook so results are returned
  hooks.RegisterHook("before_model_resolve", "test-plugin",
                     [&called](const nlohmann::json&) -> nlohmann::json {
                       called = true;
                       return {{"handled", true}};
                     });

  auto result = hooks.Fire("before_model_resolve", {{"data", 42}});
  EXPECT_TRUE(called);
  EXPECT_TRUE(result.value("handled", false));
}

TEST_F(HookManagerTest, PriorityOrdering) {
  quantclaw::HookManager hooks(logger_);

  // Use a modifying hook so handlers execute sequentially in priority order
  std::vector<int> order;
  hooks.RegisterHook(
      "before_model_resolve", "low",
      [&order](const nlohmann::json&) -> nlohmann::json {
        order.push_back(3);
        return {};
      },
      10);

  hooks.RegisterHook(
      "before_model_resolve", "high",
      [&order](const nlohmann::json&) -> nlohmann::json {
        order.push_back(1);
        return {};
      },
      100);

  hooks.RegisterHook(
      "before_model_resolve", "mid",
      [&order](const nlohmann::json&) -> nlohmann::json {
        order.push_back(2);
        return {};
      },
      50);

  hooks.Fire("before_model_resolve", {});
  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order[0], 1);  // priority 100
  EXPECT_EQ(order[1], 2);  // priority 50
  EXPECT_EQ(order[2], 3);  // priority 10
}

TEST_F(HookManagerTest, MergedResults) {
  quantclaw::HookManager hooks(logger_);

  // Use a modifying hook so results are merged
  hooks.RegisterHook("before_agent_start", "p1",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"key1", "val1"}};
                     });
  hooks.RegisterHook("before_agent_start", "p2",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"key2", "val2"}};
                     });

  auto result = hooks.Fire("before_agent_start", {});
  EXPECT_EQ(result["key1"], "val1");
  EXPECT_EQ(result["key2"], "val2");
}

TEST_F(HookManagerTest, UnregisteredHookReturnsEmpty) {
  quantclaw::HookManager hooks(logger_);
  auto result = hooks.Fire("nonexistent", {});
  EXPECT_TRUE(result.empty());
}

TEST_F(HookManagerTest, HandlerExceptionDoesNotCrash) {
  quantclaw::HookManager hooks(logger_);

  // Use a modifying hook so results are returned
  hooks.RegisterHook("message_sending", "bad",
                     [](const nlohmann::json&) -> nlohmann::json {
                       throw std::runtime_error("boom");
                     });
  hooks.RegisterHook("message_sending", "good",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"survived", true}};
                     });

  auto result = hooks.Fire("message_sending", {});
  EXPECT_TRUE(result.value("survived", false));
}

TEST_F(HookManagerTest, HandlerCount) {
  quantclaw::HookManager hooks(logger_);
  EXPECT_EQ(hooks.HandlerCount("test"), 0);

  hooks.RegisterHook("test", "a",
                     [](const nlohmann::json&) { return nlohmann::json{}; });
  hooks.RegisterHook("test", "b",
                     [](const nlohmann::json&) { return nlohmann::json{}; });
  EXPECT_EQ(hooks.HandlerCount("test"), 2);
}

TEST_F(HookManagerTest, RegisteredHooksList) {
  quantclaw::HookManager hooks(logger_);
  hooks.RegisterHook("hook_a", "p",
                     [](const nlohmann::json&) { return nlohmann::json{}; });
  hooks.RegisterHook("hook_b", "p",
                     [](const nlohmann::json&) { return nlohmann::json{}; });

  auto names = hooks.RegisteredHooks();
  ASSERT_EQ(names.size(), 2);
  // map keys are sorted
  EXPECT_EQ(names[0], "hook_a");
  EXPECT_EQ(names[1], "hook_b");
}

// --- HookMode classification tests ---

TEST(HookModeTest, ModifyingHooks) {
  EXPECT_EQ(quantclaw::GetHookMode("before_model_resolve"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("before_prompt_build"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("before_agent_start"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("message_sending"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("before_tool_call"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("subagent_spawning"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("subagent_delivery_target"),
            quantclaw::HookMode::kModifying);
}

TEST(HookModeTest, SyncHooks) {
  EXPECT_EQ(quantclaw::GetHookMode("tool_result_persist"),
            quantclaw::HookMode::kSync);
  EXPECT_EQ(quantclaw::GetHookMode("before_message_write"),
            quantclaw::HookMode::kSync);
}

TEST(HookModeTest, VoidHooks) {
  EXPECT_EQ(quantclaw::GetHookMode("llm_input"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("llm_output"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("agent_end"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("message_received"),
            quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("message_sent"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("after_tool_call"),
            quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("session_start"),
            quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("session_end"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("gateway_start"),
            quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("gateway_stop"), quantclaw::HookMode::kVoid);
}

TEST(HookModeTest, UnknownHookDefaultsToVoid) {
  EXPECT_EQ(quantclaw::GetHookMode("some_future_hook"),
            quantclaw::HookMode::kVoid);
}

TEST(HookModeTest, AllTwentyFourHooksClassified) {
  const std::vector<std::string> all_hooks = {
      "before_model_resolve", "before_prompt_build",
      "before_agent_start",   "llm_input",
      "llm_output",           "agent_end",
      "before_compaction",    "after_compaction",
      "before_reset",         "message_received",
      "message_sending",      "message_sent",
      "before_tool_call",     "after_tool_call",
      "tool_result_persist",  "before_message_write",
      "session_start",        "session_end",
      "subagent_spawning",    "subagent_delivery_target",
      "subagent_spawned",     "subagent_ended",
      "gateway_start",        "gateway_stop",
  };

  for (const auto& hook : all_hooks) {
    auto mode = quantclaw::GetHookMode(hook);
    EXPECT_TRUE(mode == quantclaw::HookMode::kVoid ||
                mode == quantclaw::HookMode::kModifying ||
                mode == quantclaw::HookMode::kSync)
        << "Hook '" << hook << "' has unexpected mode";
  }
}

TEST_F(HookManagerTest, VoidHooksRunInParallel) {
  quantclaw::HookManager hooks(logger_);

  std::atomic<int> count{0};
  for (int i = 0; i < 3; ++i) {
    hooks.RegisterHook("message_received", "p" + std::to_string(i),
                       [&count](const nlohmann::json&) -> nlohmann::json {
                         count.fetch_add(1);
                         return {{"ignored", true}};
                       });
  }

  auto result = hooks.Fire("message_received", {});
  EXPECT_EQ(count.load(), 3);
  // Void hooks return empty object (results discarded)
  EXPECT_TRUE(result.empty());
}

TEST_F(HookManagerTest, ModifyingHooksMergeResults) {
  quantclaw::HookManager hooks(logger_);

  hooks.RegisterHook(
      "before_model_resolve", "p1",
      [](const nlohmann::json&) -> nlohmann::json {
        return {{"model", "gpt-4"}};
      },
      10);
  hooks.RegisterHook(
      "before_model_resolve", "p2",
      [](const nlohmann::json&) -> nlohmann::json {
        return {{"provider", "openai"}};
      },
      5);

  auto result = hooks.Fire("before_model_resolve", {});
  EXPECT_EQ(result["model"], "gpt-4");
  EXPECT_EQ(result["provider"], "openai");
}

TEST_F(HookManagerTest, SyncHooksMergeResults) {
  quantclaw::HookManager hooks(logger_);

  hooks.RegisterHook("tool_result_persist", "p1",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"message", {{"modified", true}}}};
                     });

  auto result =
      hooks.Fire("tool_result_persist", {{"message", {{"original", true}}}});
  EXPECT_TRUE(result.contains("message"));
  EXPECT_TRUE(result["message"]["modified"].get<bool>());
}

TEST_F(HookManagerTest, ModifyingHookLaterOverridesEarlier) {
  quantclaw::HookManager hooks(logger_);

  hooks.RegisterHook(
      "before_prompt_build", "first",
      [](const nlohmann::json&) -> nlohmann::json {
        return {{"context", "initial"}};
      },
      10);
  hooks.RegisterHook(
      "before_prompt_build", "second",
      [](const nlohmann::json&) -> nlohmann::json {
        return {{"context", "override"}};
      },
      5);

  auto result = hooks.Fire("before_prompt_build", {});
  EXPECT_EQ(result["context"], "override");
}

// --- PluginOrigin / PluginStatus Helpers ---

TEST(PluginHelpersTest, OriginToString) {
  EXPECT_EQ(
      quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kBundled),
      "bundled");
  EXPECT_EQ(
      quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kGlobal),
      "global");
  EXPECT_EQ(
      quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kWorkspace),
      "workspace");
  EXPECT_EQ(
      quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kConfig),
      "config");
}

TEST(PluginHelpersTest, StatusToString) {
  EXPECT_EQ(
      quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kLoaded),
      "loaded");
  EXPECT_EQ(
      quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kDisabled),
      "disabled");
  EXPECT_EQ(quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kError),
            "error");
}

// --- PluginSystem Tests (no sidecar, manifest-only mode) ---

class PluginSystemTest : public PluginManifestTest {
 protected:
  void SetUp() override {
    PluginManifestTest::SetUp();
    logger_ = make_null_logger("plugin_sys_test");
  }

  void TearDown() override {
    PluginManifestTest::TearDown();
  }

  std::shared_ptr<spdlog::logger> logger_;
};

TEST_F(PluginSystemTest, InitializeWithNoPlugins) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  EXPECT_TRUE(sys.Initialize(config, test_dir_));
  EXPECT_TRUE(sys.Registry().Plugins().empty());
  EXPECT_FALSE(sys.IsSidecarRunning());
}

TEST_F(PluginSystemTest, InitializeDiscoversManifests) {
  auto plugins_dir = test_dir_ / "my-plugins";
  fs::create_directories(plugins_dir / "my-plugin");
  {
    std::ofstream ofs(plugins_dir / "my-plugin" / "openclaw.plugin.json");
    ofs << R"({"id":"my-plugin","skills":["weather"]})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};

  quantclaw::PluginSystem sys(logger_);
  EXPECT_TRUE(sys.Initialize(config, test_dir_));
  EXPECT_EQ(sys.Registry().Plugins().size(), 1);
  // No sidecar script found → manifest-only mode
  EXPECT_FALSE(sys.IsSidecarRunning());
}

TEST_F(PluginSystemTest, ReloadRediscoversPlugins) {
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "first");
  {
    std::ofstream ofs(plugins_dir / "first" / "openclaw.plugin.json");
    ofs << R"({"id":"first"})";
  }

  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};

  quantclaw::PluginSystem sys(logger_);
  sys.Initialize(config, test_dir_);
  EXPECT_EQ(sys.Registry().Plugins().size(), 1);

  // Add another plugin
  fs::create_directories(plugins_dir / "second");
  {
    std::ofstream ofs(plugins_dir / "second" / "openclaw.plugin.json");
    ofs << R"({"id":"second"})";
  }

  sys.Reload(config, test_dir_);
  EXPECT_EQ(sys.Registry().Plugins().size(), 2);
}

TEST_F(PluginSystemTest, HooksWorkWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  bool fired = false;
  // Use a modifying hook so results are returned
  sys.Hooks().RegisterHook("before_model_resolve", "native",
                           [&fired](const nlohmann::json&) -> nlohmann::json {
                             fired = true;
                             return {{"ok", true}};
                           });

  auto result = sys.Hooks().Fire("before_model_resolve", {});
  EXPECT_TRUE(fired);
  EXPECT_TRUE(result.value("ok", false));
}

// --- Phase 4: Plugin capability record tests ---

class PluginRegistryCapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    logger_ = make_null_logger("cap_test");
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_cap_test");
  }
  void TearDown() override {
    fs::remove_all(test_dir_);
  }
  std::shared_ptr<spdlog::logger> logger_;
  fs::path test_dir_;
};

TEST_F(PluginRegistryCapTest, UpdateFromSidecarPopulatesExistingRecord) {
  quantclaw::PluginRegistry reg(logger_);

  // Set up a plugin directory with manifest
  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "my-plugin");
  {
    std::ofstream ofs(plugins_dir / "my-plugin" / "openclaw.plugin.json");
    ofs << R"({"id":"my-plugin","name":"My Plugin"})";
  }
  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};
  reg.Discover(config, test_dir_);

  ASSERT_EQ(reg.Plugins().size(), 1);
  EXPECT_TRUE(reg.Plugins()[0].tool_names.empty());

  // Simulate sidecar plugin.list response
  nlohmann::json sidecar_list = {
      {"plugins",
       {{
           {"id", "my-plugin"},
           {"tools", {"read_file", "write_file"}},
           {"hooks", {"before_tool_call", "after_tool_call"}},
           {"services", {"file-watcher"}},
           {"providers", {"local-fs"}},
           {"commands", {"sync"}},
           {"gatewayMethods", {"fs.read"}},
           {"channels", {"stdio"}},
           {"cliEntries", {"fs-cli"}},
           {"httpHandlers", 3},
       }}},
  };

  reg.UpdateFromSidecar(sidecar_list);

  auto* rec = reg.Find("my-plugin");
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->tool_names.size(), 2);
  EXPECT_EQ(rec->tool_names[0], "read_file");
  EXPECT_EQ(rec->tool_names[1], "write_file");
  EXPECT_EQ(rec->hook_names.size(), 2);
  EXPECT_EQ(rec->service_ids.size(), 1);
  EXPECT_EQ(rec->service_ids[0], "file-watcher");
  EXPECT_EQ(rec->provider_ids.size(), 1);
  EXPECT_EQ(rec->command_names.size(), 1);
  EXPECT_EQ(rec->gateway_methods.size(), 1);
  EXPECT_EQ(rec->channel_ids.size(), 1);
  EXPECT_EQ(rec->cli_commands.size(), 1);
  EXPECT_EQ(rec->http_handler_count, 3);
}

TEST_F(PluginRegistryCapTest,
       UpdateFromSidecarCreatesNewRecordForUnknownPlugin) {
  quantclaw::PluginRegistry reg(logger_);

  // Empty registry — no plugins discovered
  quantclaw::QuantClawConfig config;
  reg.Discover(config, test_dir_);
  EXPECT_TRUE(reg.Plugins().empty());

  // Sidecar reports a plugin the registry didn't know about
  nlohmann::json sidecar_list = {
      {"plugins",
       {{
           {"id", "dynamic-plugin"},
           {"name", "Dynamic Plugin"},
           {"version", "2.0.0"},
           {"tools", {"calculate"}},
       }}},
  };

  reg.UpdateFromSidecar(sidecar_list);

  ASSERT_EQ(reg.Plugins().size(), 1);
  auto* rec = reg.Find("dynamic-plugin");
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(rec->name, "Dynamic Plugin");
  EXPECT_EQ(rec->tool_names.size(), 1);
  EXPECT_EQ(rec->tool_names[0], "calculate");
  EXPECT_TRUE(rec->enabled);
}

TEST_F(PluginRegistryCapTest, UpdateFromSidecarMergesWithoutDuplication) {
  quantclaw::PluginRegistry reg(logger_);

  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "my-plugin");
  {
    std::ofstream ofs(plugins_dir / "my-plugin" / "openclaw.plugin.json");
    ofs << R"({"id":"my-plugin","channels":["discord"],"providers":["openai"]})";
  }
  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};
  reg.Discover(config, test_dir_);

  // Sidecar reports overlapping capabilities
  nlohmann::json sidecar_list = {
      {"plugins",
       {{
           {"id", "my-plugin"},
           {"channels", {"discord", "slack"}},
           {"providers", {"openai", "anthropic"}},
       }}},
  };

  reg.UpdateFromSidecar(sidecar_list);

  auto* rec = reg.Find("my-plugin");
  ASSERT_NE(rec, nullptr);
  // Should have unique entries, no duplicates
  EXPECT_EQ(rec->channel_ids.size(), 2);
  EXPECT_EQ(rec->provider_ids.size(), 2);
}

TEST_F(PluginRegistryCapTest, UpdateFromSidecarHandlesInvalidInput) {
  quantclaw::PluginRegistry reg(logger_);
  quantclaw::QuantClawConfig config;
  reg.Discover(config, test_dir_);

  // Invalid: not an object
  reg.UpdateFromSidecar("bad input");
  EXPECT_TRUE(reg.Plugins().empty());

  // Invalid: missing plugins key
  reg.UpdateFromSidecar({{"foo", "bar"}});
  EXPECT_TRUE(reg.Plugins().empty());

  // Invalid: plugins is not array
  reg.UpdateFromSidecar({{"plugins", "not-an-array"}});
  EXPECT_TRUE(reg.Plugins().empty());

  // Valid but empty
  reg.UpdateFromSidecar({{"plugins", nlohmann::json::array()}});
  EXPECT_TRUE(reg.Plugins().empty());
}

TEST_F(PluginRegistryCapTest, UpdateFromSidecarSkipsEntriesWithoutId) {
  quantclaw::PluginRegistry reg(logger_);
  quantclaw::QuantClawConfig config;
  reg.Discover(config, test_dir_);

  nlohmann::json sidecar_list = {
      {"plugins",
       {
           {{"tools", {"tool1"}}},              // no id
           {{"id", ""}, {"tools", {"tool2"}}},  // empty id
           {{"id", "valid"}, {"tools", {"tool3"}}},
       }},
  };

  reg.UpdateFromSidecar(sidecar_list);

  // Only "valid" should be added
  ASSERT_EQ(reg.Plugins().size(), 1);
  EXPECT_EQ(reg.Plugins()[0].id, "valid");
}

TEST_F(PluginRegistryCapTest, ToJsonIncludesCapabilities) {
  quantclaw::PluginRegistry reg(logger_);

  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "full-plugin");
  {
    std::ofstream ofs(plugins_dir / "full-plugin" / "openclaw.plugin.json");
    ofs << R"({"id":"full-plugin","name":"Full"})";
  }
  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};
  reg.Discover(config, test_dir_);

  nlohmann::json sidecar_list = {
      {"plugins",
       {{
           {"id", "full-plugin"},
           {"tools", {"t1"}},
           {"hooks", {"h1"}},
           {"services", {"s1"}},
           {"httpHandlers", 2},
       }}},
  };
  reg.UpdateFromSidecar(sidecar_list);

  auto json = reg.ToJson();
  ASSERT_EQ(json.size(), 1);
  EXPECT_TRUE(json[0].contains("tools"));
  EXPECT_EQ(json[0]["tools"].size(), 1);
  EXPECT_TRUE(json[0].contains("hooks"));
  EXPECT_TRUE(json[0].contains("services"));
  EXPECT_EQ(json[0]["httpHandlers"], 2);
}

TEST_F(PluginRegistryCapTest, PluginRecordNewFields) {
  quantclaw::PluginRecord rec;
  // Default state: all capability lists empty
  EXPECT_TRUE(rec.tool_names.empty());
  EXPECT_TRUE(rec.service_ids.empty());
  EXPECT_TRUE(rec.command_names.empty());
  EXPECT_EQ(rec.http_handler_count, 0);
}

TEST_F(PluginRegistryCapTest, MultiplePluginsUpdated) {
  quantclaw::PluginRegistry reg(logger_);

  auto plugins_dir = test_dir_ / "plugins";
  fs::create_directories(plugins_dir / "p1");
  fs::create_directories(plugins_dir / "p2");
  {
    std::ofstream ofs(plugins_dir / "p1" / "openclaw.plugin.json");
    ofs << R"({"id":"p1"})";
  }
  {
    std::ofstream ofs(plugins_dir / "p2" / "openclaw.plugin.json");
    ofs << R"({"id":"p2"})";
  }
  quantclaw::QuantClawConfig config;
  config.plugins_config = {{"load", {{"paths", {plugins_dir.string()}}}}};
  reg.Discover(config, test_dir_);

  nlohmann::json sidecar_list = {
      {"plugins",
       {
           {{"id", "p1"}, {"tools", {"a", "b"}}},
           {{"id", "p2"}, {"tools", {"c"}}, {"services", {"svc"}}},
       }},
  };
  reg.UpdateFromSidecar(sidecar_list);

  auto* r1 = reg.Find("p1");
  auto* r2 = reg.Find("p2");
  ASSERT_NE(r1, nullptr);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r1->tool_names.size(), 2);
  EXPECT_EQ(r2->tool_names.size(), 1);
  EXPECT_EQ(r2->service_ids.size(), 1);
}

// --- PluginSystem convenience method tests (no sidecar) ---

TEST_F(PluginSystemTest, ListServicesWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto result = sys.ListServices();
  EXPECT_TRUE(result.is_array());
  EXPECT_TRUE(result.empty());
}

TEST_F(PluginSystemTest, ListProvidersWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto result = sys.ListProviders();
  EXPECT_TRUE(result.is_array());
  EXPECT_TRUE(result.empty());
}

TEST_F(PluginSystemTest, ListCommandsWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto result = sys.ListCommands();
  EXPECT_TRUE(result.is_array());
  EXPECT_TRUE(result.empty());
}

TEST_F(PluginSystemTest, ListGatewayMethodsWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto result = sys.ListGatewayMethods();
  EXPECT_TRUE(result.is_array());
  EXPECT_TRUE(result.empty());
}

TEST_F(PluginSystemTest, ExecuteCommandWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto result = sys.ExecuteCommand("test", {});
  EXPECT_TRUE(result.contains("error"));
}

TEST_F(PluginSystemTest, StartStopServiceWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.Initialize(config, test_dir_);

  auto start_result = sys.StartService("svc");
  EXPECT_TRUE(start_result.contains("error"));

  auto stop_result = sys.StopService("svc");
  EXPECT_TRUE(stop_result.contains("error"));
}

// ================================================================
// P4 — Extended HookManager Tests
// ================================================================

TEST_F(HookManagerTest, UnregisterSpecificHandler) {
  quantclaw::HookManager hooks(logger_);

  std::vector<std::string> calls;
  hooks.RegisterHook("before_model_resolve", "plugin-a",
                     [&calls](const nlohmann::json&) -> nlohmann::json {
                       calls.push_back("a");
                       return {{"from", "a"}};
                     });
  hooks.RegisterHook("before_model_resolve", "plugin-b",
                     [&calls](const nlohmann::json&) -> nlohmann::json {
                       calls.push_back("b");
                       return {{"from", "b"}};
                     });

  EXPECT_EQ(hooks.HandlerCount("before_model_resolve"), 2u);

  // Unregister plugin-a
  bool removed = hooks.UnregisterHook("before_model_resolve", "plugin-a");
  EXPECT_TRUE(removed);
  EXPECT_EQ(hooks.HandlerCount("before_model_resolve"), 1u);

  // Fire and verify only plugin-b runs
  calls.clear();
  hooks.Fire("before_model_resolve", {});
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0], "b");
}

TEST_F(HookManagerTest, ErrorInHandlerDoesNotBreakOthers) {
  quantclaw::HookManager hooks(logger_);

  std::vector<std::string> calls;
  // Use a modifying hook so handlers run sequentially
  hooks.RegisterHook(
      "before_prompt_build", "bad-handler",
      [](const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("handler exploded");
      },
      100);  // Higher priority, runs first
  hooks.RegisterHook(
      "before_prompt_build", "good-handler",
      [&calls](const nlohmann::json&) -> nlohmann::json {
        calls.push_back("good");
        return {{"ok", true}};
      },
      50);  // Lower priority, runs second

  auto result = hooks.Fire("before_prompt_build", {});
  // good-handler should have run despite bad-handler throwing
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0], "good");
  EXPECT_TRUE(result.value("ok", false));
}

TEST_F(HookManagerTest, HookModeForClassification) {
  // Verify at least 5 hook names return correct modes
  EXPECT_EQ(quantclaw::GetHookMode("before_model_resolve"),
            quantclaw::HookMode::kModifying);
  EXPECT_EQ(quantclaw::GetHookMode("llm_input"), quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("tool_result_persist"),
            quantclaw::HookMode::kSync);
  EXPECT_EQ(quantclaw::GetHookMode("message_received"),
            quantclaw::HookMode::kVoid);
  EXPECT_EQ(quantclaw::GetHookMode("before_tool_call"),
            quantclaw::HookMode::kModifying);
}

TEST_F(HookManagerTest, ClearAllHandlers) {
  quantclaw::HookManager hooks(logger_);

  hooks.RegisterHook("before_model_resolve", "p1",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"cleared", false}};
                     });
  hooks.RegisterHook("llm_input", "p2",
                     [](const nlohmann::json&) -> nlohmann::json {
                       return {{"cleared", false}};
                     });

  EXPECT_EQ(hooks.HandlerCount("before_model_resolve"), 1u);
  EXPECT_EQ(hooks.HandlerCount("llm_input"), 1u);
  EXPECT_EQ(hooks.RegisteredHooks().size(), 2u);

  hooks.Clear();

  EXPECT_EQ(hooks.HandlerCount("before_model_resolve"), 0u);
  EXPECT_EQ(hooks.HandlerCount("llm_input"), 0u);
  EXPECT_TRUE(hooks.RegisteredHooks().empty());

  // Fire should return empty after clear
  auto result = hooks.Fire("before_model_resolve", {});
  EXPECT_TRUE(result.empty());
}
