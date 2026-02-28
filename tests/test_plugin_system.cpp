#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "quantclaw/plugins/plugin_manifest.hpp"
#include "quantclaw/plugins/plugin_registry.hpp"
#include "quantclaw/plugins/hook_manager.hpp"
#include "quantclaw/plugins/plugin_system.hpp"
#include "quantclaw/config.hpp"

namespace fs = std::filesystem;

static std::shared_ptr<spdlog::logger> make_null_logger(const std::string& name) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  return std::make_shared<spdlog::logger>(name, null_sink);
}

class PluginManifestTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = fs::temp_directory_path() / "quantclaw_test_plugins";
    fs::create_directories(test_dir_);
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
  auto m = quantclaw::PluginManifest::parse(j);
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
  auto m = quantclaw::PluginManifest::parse(j);
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
  EXPECT_THROW(quantclaw::PluginManifest::parse(j), std::runtime_error);
}

TEST_F(PluginManifestTest, LoadFromFile) {
  nlohmann::json manifest = {
      {"id", "file-test"},
      {"name", "File Test Plugin"},
      {"version", "0.1.0"},
  };
  auto path = write_manifest("file-test", manifest);
  auto m = quantclaw::PluginManifest::load_from_file(path);
  EXPECT_EQ(m.id, "file-test");
  EXPECT_EQ(m.name, "File Test Plugin");
}

TEST_F(PluginManifestTest, LoadFromNonexistentFileThrows) {
  EXPECT_THROW(
      quantclaw::PluginManifest::load_from_file("/nonexistent/path.json"),
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
  auto m = quantclaw::PluginManifest::parse(j);
  auto out = m.to_json();
  EXPECT_EQ(out["id"], "roundtrip");
  EXPECT_EQ(out["name"], "Roundtrip Test");
  EXPECT_EQ(out["channels"].size(), 2);
}

TEST_F(PluginManifestTest, ParseUiHints) {
  nlohmann::json j = {
      {"id", "hints"},
      {"uiHints", {
          {"apiKey", {
              {"label", "API Key"},
              {"help", "Your secret key"},
              {"sensitive", true},
              {"advanced", false},
              {"tags", {"auth", "security"}},
          }},
      }},
  };
  auto m = quantclaw::PluginManifest::parse(j);
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
  reg.discover(config, test_dir_);
  EXPECT_TRUE(reg.plugins().empty());
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
  reg.discover(config, test_dir_);

  EXPECT_EQ(reg.plugins().size(), 2);
  EXPECT_TRUE(reg.find("plugin-a") != nullptr);
  EXPECT_TRUE(reg.find("plugin-b") != nullptr);
  EXPECT_EQ(reg.find("plugin-a")->name, "Plugin A");
  ASSERT_EQ(reg.find("plugin-b")->channel_ids.size(), 1);
  EXPECT_EQ(reg.find("plugin-b")->channel_ids[0], "telegram");
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
  reg.discover(config, test_dir_);

  EXPECT_TRUE(reg.is_enabled("enabled-one"));
  EXPECT_FALSE(reg.is_enabled("disabled-one"));
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
  reg.discover(config, test_dir_);

  EXPECT_TRUE(reg.is_enabled("allowed"));
  EXPECT_FALSE(reg.is_enabled("not-allowed"));
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
  reg.discover(config, test_dir_);

  auto j = reg.to_json();
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
  reg.discover(config, test_dir_);

  EXPECT_TRUE(reg.find("qc-plugin") != nullptr);
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
  reg.discover(config, test_dir_);

  EXPECT_FALSE(reg.is_enabled("some-plugin"));
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
  reg.discover(config, test_dir_);

  auto* rec = reg.find("cfg-plugin");
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
  hooks.register_hook("test_hook", "test-plugin",
                      [&called](const nlohmann::json&) -> nlohmann::json {
                        called = true;
                        return {{"handled", true}};
                      });

  auto result = hooks.fire("test_hook", {{"data", 42}});
  EXPECT_TRUE(called);
  EXPECT_TRUE(result.value("handled", false));
}

TEST_F(HookManagerTest, PriorityOrdering) {
  quantclaw::HookManager hooks(logger_);

  std::vector<int> order;
  hooks.register_hook("ordered_hook", "low",
                      [&order](const nlohmann::json&) -> nlohmann::json {
                        order.push_back(3);
                        return {};
                      }, 10);

  hooks.register_hook("ordered_hook", "high",
                      [&order](const nlohmann::json&) -> nlohmann::json {
                        order.push_back(1);
                        return {};
                      }, 100);

  hooks.register_hook("ordered_hook", "mid",
                      [&order](const nlohmann::json&) -> nlohmann::json {
                        order.push_back(2);
                        return {};
                      }, 50);

  hooks.fire("ordered_hook", {});
  ASSERT_EQ(order.size(), 3);
  EXPECT_EQ(order[0], 1);  // priority 100
  EXPECT_EQ(order[1], 2);  // priority 50
  EXPECT_EQ(order[2], 3);  // priority 10
}

TEST_F(HookManagerTest, MergedResults) {
  quantclaw::HookManager hooks(logger_);

  hooks.register_hook("merge_hook", "p1",
                      [](const nlohmann::json&) -> nlohmann::json {
                        return {{"key1", "val1"}};
                      });
  hooks.register_hook("merge_hook", "p2",
                      [](const nlohmann::json&) -> nlohmann::json {
                        return {{"key2", "val2"}};
                      });

  auto result = hooks.fire("merge_hook", {});
  EXPECT_EQ(result["key1"], "val1");
  EXPECT_EQ(result["key2"], "val2");
}

TEST_F(HookManagerTest, UnregisteredHookReturnsEmpty) {
  quantclaw::HookManager hooks(logger_);
  auto result = hooks.fire("nonexistent", {});
  EXPECT_TRUE(result.empty());
}

TEST_F(HookManagerTest, HandlerExceptionDoesNotCrash) {
  quantclaw::HookManager hooks(logger_);

  hooks.register_hook("throw_hook", "bad",
                      [](const nlohmann::json&) -> nlohmann::json {
                        throw std::runtime_error("boom");
                      });
  hooks.register_hook("throw_hook", "good",
                      [](const nlohmann::json&) -> nlohmann::json {
                        return {{"survived", true}};
                      });

  auto result = hooks.fire("throw_hook", {});
  EXPECT_TRUE(result.value("survived", false));
}

TEST_F(HookManagerTest, HandlerCount) {
  quantclaw::HookManager hooks(logger_);
  EXPECT_EQ(hooks.handler_count("test"), 0);

  hooks.register_hook("test", "a", [](const nlohmann::json&) { return nlohmann::json{}; });
  hooks.register_hook("test", "b", [](const nlohmann::json&) { return nlohmann::json{}; });
  EXPECT_EQ(hooks.handler_count("test"), 2);
}

TEST_F(HookManagerTest, RegisteredHooksList) {
  quantclaw::HookManager hooks(logger_);
  hooks.register_hook("hook_a", "p", [](const nlohmann::json&) { return nlohmann::json{}; });
  hooks.register_hook("hook_b", "p", [](const nlohmann::json&) { return nlohmann::json{}; });

  auto names = hooks.registered_hooks();
  ASSERT_EQ(names.size(), 2);
  // map keys are sorted
  EXPECT_EQ(names[0], "hook_a");
  EXPECT_EQ(names[1], "hook_b");
}

// --- PluginOrigin / PluginStatus Helpers ---

TEST(PluginHelpersTest, OriginToString) {
  EXPECT_EQ(quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kBundled), "bundled");
  EXPECT_EQ(quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kGlobal), "global");
  EXPECT_EQ(quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kWorkspace), "workspace");
  EXPECT_EQ(quantclaw::plugin_origin_to_string(quantclaw::PluginOrigin::kConfig), "config");
}

TEST(PluginHelpersTest, StatusToString) {
  EXPECT_EQ(quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kLoaded), "loaded");
  EXPECT_EQ(quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kDisabled), "disabled");
  EXPECT_EQ(quantclaw::plugin_status_to_string(quantclaw::PluginStatus::kError), "error");
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
  EXPECT_TRUE(sys.initialize(config, test_dir_));
  EXPECT_TRUE(sys.registry().plugins().empty());
  EXPECT_FALSE(sys.is_sidecar_running());
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
  EXPECT_TRUE(sys.initialize(config, test_dir_));
  EXPECT_EQ(sys.registry().plugins().size(), 1);
  // No sidecar script found → manifest-only mode
  EXPECT_FALSE(sys.is_sidecar_running());
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
  sys.initialize(config, test_dir_);
  EXPECT_EQ(sys.registry().plugins().size(), 1);

  // Add another plugin
  fs::create_directories(plugins_dir / "second");
  {
    std::ofstream ofs(plugins_dir / "second" / "openclaw.plugin.json");
    ofs << R"({"id":"second"})";
  }

  sys.reload(config, test_dir_);
  EXPECT_EQ(sys.registry().plugins().size(), 2);
}

TEST_F(PluginSystemTest, HooksWorkWithoutSidecar) {
  quantclaw::PluginSystem sys(logger_);
  quantclaw::QuantClawConfig config;
  sys.initialize(config, test_dir_);

  bool fired = false;
  sys.hooks().register_hook("test", "native",
                            [&fired](const nlohmann::json&) -> nlohmann::json {
                              fired = true;
                              return {{"ok", true}};
                            });

  auto result = sys.hooks().fire("test", {});
  EXPECT_TRUE(fired);
  EXPECT_TRUE(result["ok"].get<bool>());
}
