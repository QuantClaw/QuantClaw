// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/core/skill_loader.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

class SkillLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = quantclaw::test::MakeTestDir("quantclaw_skills_test");

    auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("test", null_sink);

    skill_loader_ = std::make_unique<quantclaw::SkillLoader>(logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  void write_skill(const std::string& name, const std::string& content) {
    auto dir = test_dir_ / name;
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "SKILL.md");
    f << content;
    f.close();
  }

  std::filesystem::path test_dir_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SkillLoader> skill_loader_;
};

TEST_F(SkillLoaderTest, LoadSimpleSkill) {
  write_skill("test-skill", R"(---
name: test-skill
description: A simple test skill
---

# Test Skill

This is a test skill for QuantClaw.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);

  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].name, "test-skill");
  EXPECT_EQ(skills[0].description, "A simple test skill");
  EXPECT_TRUE(skills[0].content.find("This is a test skill") !=
              std::string::npos);
}

TEST_F(SkillLoaderTest, SkillWithNoRequirements) {
  write_skill("simple", R"(---
name: simple
description: No requirements
---

Content here.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);

  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].required_bins.size(), 0u);
  EXPECT_EQ(skills[0].required_envs.size(), 0u);
  EXPECT_EQ(skills[0].any_bins.size(), 0u);
  EXPECT_EQ(skills[0].config_files.size(), 0u);
  EXPECT_EQ(skills[0].os_restrict.size(), 0u);
  EXPECT_FALSE(skills[0].always);
}

TEST_F(SkillLoaderTest, SkillGatedByMissingEnv) {
  write_skill("weather", R"(---
name: weather
description: Weather skill
requires:
  env:
    - WEATHER_API_KEY_NONEXISTENT
---

Weather content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  EXPECT_EQ(skills.size(), 0u);
}

TEST_F(SkillLoaderTest, SkillWithAlwaysFlag) {
  write_skill("always-on", R"(---
name: always-on
description: Always loaded
always: true
requires:
  env:
    - NONEXISTENT_VAR_XYZ
---

Always content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].name, "always-on");
  EXPECT_TRUE(skills[0].always);
}

TEST_F(SkillLoaderTest, SkillWithEmoji) {
  write_skill("emoji-skill", R"(---
name: emoji-skill
description: Has emoji
emoji: rocket
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].emoji, "rocket");
}

TEST_F(SkillLoaderTest, SkillWithPrimaryEnv) {
  write_skill("env-skill", R"(---
name: env-skill
description: Has primary env
primaryEnv: MY_KEY
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].primary_env, "MY_KEY");
}

TEST_F(SkillLoaderTest, SkillWithOsRestriction) {
  write_skill("os-skill", R"(---
name: os-skill
description: Linux only
os: ["linux"]
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
#ifdef __linux__
  EXPECT_EQ(skills.size(), 1u);
#elif defined(__APPLE__)
  EXPECT_EQ(skills.size(), 0u);
#endif
}

TEST_F(SkillLoaderTest, SkillContextOutput) {
  write_skill("ctx-skill", R"(---
name: ctx-skill
description: Context test
emoji: star
---

Skill body text.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  auto context = skill_loader_->GetSkillContext(skills);

  EXPECT_TRUE(context.find("ctx-skill") != std::string::npos);
  EXPECT_TRUE(context.find("Context test") != std::string::npos);
  EXPECT_TRUE(context.find("Skill body text") != std::string::npos);
  EXPECT_TRUE(context.find("star") != std::string::npos);
}

TEST_F(SkillLoaderTest, NonExistentDirectory) {
  auto skills =
      skill_loader_->LoadSkillsFromDirectory(test_dir_ / "nonexistent");
  EXPECT_EQ(skills.size(), 0u);
}

TEST_F(SkillLoaderTest, MultipleSkills) {
  write_skill("skill-a", "---\nname: skill-a\ndescription: A\n---\nA content.");
  write_skill("skill-b", "---\nname: skill-b\ndescription: B\n---\nB content.");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  EXPECT_EQ(skills.size(), 2u);
}

TEST_F(SkillLoaderTest, SkillDefaultsNameFromDirectory) {
  write_skill("dir-name", R"(---
description: No name field
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].name, "dir-name");
}

// --- OpenClaw compatibility tests ---

TEST_F(SkillLoaderTest, NestedOpenClawRequiresFormat) {
  write_skill("nested-skill", R"(---
name: nested-skill
description: Nested requires
metadata:
  openclaw:
    requires:
      env:
        - NONEXISTENT_OPENCLAW_KEY_XYZ
---

Nested content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  // Should be gated out because NONEXISTENT_OPENCLAW_KEY_XYZ doesn't exist
  EXPECT_EQ(skills.size(), 0u);
}

TEST_F(SkillLoaderTest, NestedOpenClawWithAlwaysFlag) {
  write_skill("nested-always", R"(---
name: nested-always
description: Nested with always
always: true
metadata:
  openclaw:
    requires:
      env:
        - NONEXISTENT_OPENCLAW_KEY_XYZ
---

Always loaded nested content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].name, "nested-always");
  EXPECT_TRUE(skills[0].always);
}

TEST_F(SkillLoaderTest, MacosAliasForDarwin) {
  write_skill("macos-skill", R"(---
name: macos-skill
description: macOS only
os: ["macos"]
---

macOS content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
#ifdef __linux__
  // "macos" normalizes to "darwin", which != "linux"
  EXPECT_EQ(skills.size(), 0u);
#elif defined(__APPLE__)
  // "macos" normalizes to "darwin", which == "darwin"
  EXPECT_EQ(skills.size(), 1u);
#endif
}

TEST_F(SkillLoaderTest, LoadSkillsMultiDir) {
  // Create two separate directories with different skills
  auto dir_a = quantclaw::test::MakeTestDir("quantclaw_multi_a");
  auto dir_b = quantclaw::test::MakeTestDir("quantclaw_multi_b");
  std::filesystem::create_directories(dir_a / "skill-a");
  std::filesystem::create_directories(dir_b / "skill-b");

  {
    std::ofstream f(dir_a / "skill-a" / "SKILL.md");
    f << "---\nname: skill-a\ndescription: A\n---\nA content.";
  }
  {
    std::ofstream f(dir_b / "skill-b" / "SKILL.md");
    f << "---\nname: skill-b\ndescription: B\n---\nB content.";
  }

  // Use workspace_path = dir_a's parent (skills/ subdir = dir_a)
  // and extraDirs = [dir_b]
  auto workspace = quantclaw::test::MakeTestDir("quantclaw_multi_ws");
  // Symlink or copy dir_a as workspace/skills
  auto ws_skills = workspace / "skills";
  if (std::filesystem::exists(ws_skills))
    std::filesystem::remove_all(ws_skills);
  std::filesystem::create_directories(ws_skills / "skill-a");
  {
    std::ofstream f(ws_skills / "skill-a" / "SKILL.md");
    f << "---\nname: skill-a\ndescription: A\n---\nA content.";
  }

  quantclaw::SkillsConfig config;
  config.load.extra_dirs.push_back(dir_b.string());

  auto skills = skill_loader_->LoadSkills(config, workspace);

  // Should find skill-a from workspace/skills and skill-b from extra dir
  EXPECT_GE(skills.size(), 2u);

  bool found_a = false, found_b = false;
  for (const auto& s : skills) {
    if (s.name == "skill-a")
      found_a = true;
    if (s.name == "skill-b")
      found_b = true;
  }
  EXPECT_TRUE(found_a);
  EXPECT_TRUE(found_b);

  // Cleanup
  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);
  std::filesystem::remove_all(workspace);
}

TEST_F(SkillLoaderTest, DeduplicationWorkspaceWins) {
  auto workspace = quantclaw::test::MakeTestDir("quantclaw_dedup_ws");
  auto extra_dir = quantclaw::test::MakeTestDir("quantclaw_dedup_extra");
  std::filesystem::create_directories(workspace / "skills" / "dupe-skill");
  std::filesystem::create_directories(extra_dir / "dupe-skill");

  {
    std::ofstream f(workspace / "skills" / "dupe-skill" / "SKILL.md");
    f << "---\nname: dupe-skill\ndescription: workspace "
         "version\n---\nWorkspace.";
  }
  {
    std::ofstream f(extra_dir / "dupe-skill" / "SKILL.md");
    f << "---\nname: dupe-skill\ndescription: extra version\n---\nExtra.";
  }

  quantclaw::SkillsConfig config;
  config.load.extra_dirs.push_back(extra_dir.string());

  auto skills = skill_loader_->LoadSkills(config, workspace);

  // Should only have one instance, from workspace (first wins)
  int count = 0;
  for (const auto& s : skills) {
    if (s.name == "dupe-skill") {
      ++count;
      EXPECT_EQ(s.description, "workspace version");
    }
  }
  EXPECT_EQ(count, 1);

  std::filesystem::remove_all(workspace);
  std::filesystem::remove_all(extra_dir);
}

TEST_F(SkillLoaderTest, PerSkillDisableViaConfig) {
  write_skill("enabled-skill",
              "---\nname: enabled-skill\ndescription: E\n---\nE.");
  write_skill("disabled-skill",
              "---\nname: disabled-skill\ndescription: D\n---\nD.");

  // Use load_skills with a config that disables one skill
  // We need to set up workspace pointing to test_dir_ as skills subdir
  auto workspace = quantclaw::test::MakeTestDir("quantclaw_disable_ws");
  auto ws_skills = workspace / "skills";
  if (std::filesystem::exists(workspace))
    std::filesystem::remove_all(workspace);
  std::filesystem::create_directories(ws_skills);

  // Copy skills into workspace/skills
  for (const auto& entry : std::filesystem::directory_iterator(test_dir_)) {
    if (entry.is_directory()) {
      auto dest = ws_skills / entry.path().filename();
      std::filesystem::copy(entry.path(), dest,
                            std::filesystem::copy_options::recursive);
    }
  }

  quantclaw::SkillsConfig config;
  config.entries["disabled-skill"] = quantclaw::SkillEntryConfig{false};

  auto skills = skill_loader_->LoadSkills(config, workspace);

  bool found_enabled = false, found_disabled = false;
  for (const auto& s : skills) {
    if (s.name == "enabled-skill")
      found_enabled = true;
    if (s.name == "disabled-skill")
      found_disabled = true;
  }
  EXPECT_TRUE(found_enabled);
  EXPECT_FALSE(found_disabled);

  std::filesystem::remove_all(workspace);
}

// --- OpenClaw format compatibility tests ---

TEST_F(SkillLoaderTest, OpenClawInstallArrayFormat) {
  // OpenClaw uses JSON-style install arrays. Our simple YAML parser does not
  // handle arrays of objects, but the code paths for parsing install arrays
  // are tested here via the JSON install section (QuantClaw object format).
  // The metadata.openclaw fields (emoji, homepage, etc.) are tested below.
  write_skill("weather", R"YAML(---
name: weather
description: Get weather forecast
emoji: "⛅"
homepage: "https://weather.example.com"
skillKey: "weather-v2"
requires:
  bins:
    - curl
install:
  brew: curl
  node: "@weather/cli"
---

# Weather Skill
)YAML");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  EXPECT_EQ(s.name, "weather");
  EXPECT_EQ(s.emoji, "⛅");
  EXPECT_EQ(s.homepage, "https://weather.example.com");
  EXPECT_EQ(s.skill_key, "weather-v2");
  ASSERT_EQ(s.required_bins.size(), 1);
  EXPECT_EQ(s.required_bins[0], "curl");

  ASSERT_EQ(s.installs.size(), 2);

  // Both install methods should parse correctly
  bool found_brew = false, found_node = false;
  for (const auto& inst : s.installs) {
    if (inst.EffectiveMethod() == "brew") {
      EXPECT_EQ(inst.EffectiveFormula(), "curl");
      found_brew = true;
    }
    if (inst.EffectiveMethod() == "node") {
      EXPECT_EQ(inst.EffectiveFormula(), "@weather/cli");
      found_node = true;
    }
  }
  EXPECT_TRUE(found_brew);
  EXPECT_TRUE(found_node);
}

TEST_F(SkillLoaderTest, QuantClawObjectInstallFormat) {
  write_skill("tools", R"(---
name: tools
install:
  apt: build-essential
  node: typescript
---

# Tools
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  ASSERT_EQ(s.installs.size(), 2);
  // Both formats should produce valid EffectiveMethod/Formula
  bool found_apt = false, found_node = false;
  for (const auto& inst : s.installs) {
    if (inst.EffectiveMethod() == "apt") {
      EXPECT_EQ(inst.EffectiveFormula(), "build-essential");
      found_apt = true;
    }
    if (inst.EffectiveMethod() == "node") {
      EXPECT_EQ(inst.EffectiveFormula(), "typescript");
      found_node = true;
    }
  }
  EXPECT_TRUE(found_apt);
  EXPECT_TRUE(found_node);
}

TEST_F(SkillLoaderTest, OpenClawMetadataFieldsFallback) {
  // Metadata.openclaw fields should populate top-level fields
  write_skill("meta-test", R"YAML(---
name: meta-test
metadata:
  openclaw:
    emoji: "🔧"
    primaryEnv: "MY_API_KEY"
    always: true
    os:
      - linux
      - darwin
---

# Meta Test
)YAML");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  EXPECT_EQ(s.emoji, "🔧");
  EXPECT_EQ(s.primary_env, "MY_API_KEY");
  EXPECT_TRUE(s.always);
  ASSERT_EQ(s.os_restrict.size(), 2);
  EXPECT_EQ(s.os_restrict[0], "linux");
  EXPECT_EQ(s.os_restrict[1], "darwin");
}

TEST_F(SkillLoaderTest, FlatFieldsNotOverriddenByMetadata) {
  // Top-level fields should take precedence over metadata.openclaw
  write_skill("priority-test", R"YAML(---
name: priority-test
emoji: "🎯"
primaryEnv: "TOP_LEVEL"
metadata:
  openclaw:
    emoji: "🔧"
    primaryEnv: "NESTED"
---

# Priority Test
)YAML");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  // Top-level fields win
  EXPECT_EQ(s.emoji, "🎯");
  EXPECT_EQ(s.primary_env, "TOP_LEVEL");
}

TEST_F(SkillLoaderTest, InstallInfoEffectiveMethods) {
  quantclaw::SkillInstallInfo info;

  // Empty defaults
  EXPECT_EQ(info.EffectiveMethod(), "");
  EXPECT_EQ(info.EffectiveFormula(), "");
  EXPECT_EQ(info.EffectiveBinary(), "");

  // Method only
  info.method = "apt";
  EXPECT_EQ(info.EffectiveMethod(), "apt");

  // Kind overrides method
  info.kind = "brew";
  EXPECT_EQ(info.EffectiveMethod(), "brew");

  // Formula priority: formula > package > url
  info.url = "https://example.com/bin";
  EXPECT_EQ(info.EffectiveFormula(), "https://example.com/bin");
  info.package = "@scope/pkg";
  EXPECT_EQ(info.EffectiveFormula(), "@scope/pkg");
  info.formula = "explicit-formula";
  EXPECT_EQ(info.EffectiveFormula(), "explicit-formula");

  // Binary priority: binary > bins[0]
  info.bins = {"bin1", "bin2"};
  EXPECT_EQ(info.EffectiveBinary(), "bin1");
  info.binary = "explicit-bin";
  EXPECT_EQ(info.EffectiveBinary(), "explicit-bin");
}

TEST_F(SkillLoaderTest, HomepageAndSkillKeyFromTopLevel) {
  write_skill("topfields", R"(---
name: topfields
homepage: "https://example.com"
skillKey: "my-key"
---

# Top Fields
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1);
  EXPECT_EQ(skills[0].homepage, "https://example.com");
  EXPECT_EQ(skills[0].skill_key, "my-key");
}

// ── Search skill
// ──────────────────────────────────────────────────────────────

TEST_F(SkillLoaderTest, SearchSkillAlwaysLoaded) {
  // always:true means the skill loads regardless of env/binary availability.
  write_skill("search", R"(---
name: search
emoji: "🔍"
description: Web search with automatic provider fallback (Tavily -> DuckDuckGo)
always: true
commands:
  - name: search
    description: Search the web for a query
    toolName: web_search
    argMode: freeform
---

Use the `web_search` tool to search the web.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].name, "search");
  EXPECT_TRUE(skills[0].always);
}

TEST_F(SkillLoaderTest, SearchSkillHasSearchCommand) {
  write_skill("search", R"(---
name: search
always: true
commands:
  - name: search
    description: Search the web for a query
    toolName: web_search
    argMode: freeform
---

Use the `web_search` tool.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);

  const auto& cmds = skills[0].commands;
  ASSERT_EQ(cmds.size(), 1u);
  EXPECT_EQ(cmds[0].name, "search");
  EXPECT_EQ(cmds[0].tool_name, "web_search");
  EXPECT_EQ(cmds[0].arg_mode, "freeform");
}

TEST_F(SkillLoaderTest, SearchSkillContentMentionsProviders) {
  write_skill("search", R"(---
name: search
always: true
---

Use the `web_search` tool to search the web.
Providers: Tavily (TAVILY_API_KEY), DuckDuckGo (free fallback).
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_NE(skills[0].content.find("web_search"), std::string::npos);
  EXPECT_NE(skills[0].content.find("Tavily"), std::string::npos);
  EXPECT_NE(skills[0].content.find("DuckDuckGo"), std::string::npos);
}

TEST_F(SkillLoaderTest, SearchSkillNoRequiredBinsOrEnvs) {
  // DuckDuckGo fallback requires no API key, so no hard gating.
  write_skill("search", R"(---
name: search
always: true
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_TRUE(skills[0].required_bins.empty());
  EXPECT_TRUE(skills[0].required_envs.empty());
}

TEST_F(SkillLoaderTest, SearchSkillGetAllCommandsIncludesSearch) {
  write_skill("search", R"(---
name: search
always: true
commands:
  - name: search
    description: Search the web
    toolName: web_search
    argMode: freeform
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  auto all_cmds = skill_loader_->GetAllCommands(skills);

  ASSERT_EQ(all_cmds.size(), 1u);
  EXPECT_EQ(all_cmds[0].name, "search");
  EXPECT_EQ(all_cmds[0].tool_name, "web_search");
}

TEST_F(SkillLoaderTest, SearchSkillContextOutputContainsName) {
  write_skill("search", R"(---
name: search
emoji: "🔍"
description: Web search
always: true
---

Use `web_search` for queries.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  std::string ctx = skill_loader_->GetSkillContext(skills);

  EXPECT_NE(ctx.find("search"), std::string::npos);
  EXPECT_NE(ctx.find("web_search"), std::string::npos);
}

// --- Regression: colon-containing scalar strings in arrays (PR #24 review) ---
// Copilot noted that strings like "https://example.com" or "12:34" could be
// misclassified as object items because the text before the colon passes the
// alphanumeric key-character check.  The fix requires a space/tab immediately
// after the colon; these tests guard against regressions.
//
// Observable impact: if a URL in "bins:" is misclassified as an object, the
// subsequent get<vector<string>>() throws and the entire frontmatter parse
// is aborted, leaving skill.commands empty even if they were defined first.

TEST_F(SkillLoaderTest, UrlInBinsArrayParsedAsString) {
  // A URL in the bins: array must remain a plain string.  Without the fix,
  // "https://example.com" is parsed as object {https: "//example.com"},
  // causing get<vector<string>>() to throw and aborting the whole parse —
  // so commands end up empty even though they appeared before bins in the
  // frontmatter processing.
  write_skill("url-bins-skill", R"(---
name: url-bins-skill
description: Skill whose bins list contains a URL
always: true
commands:
  - name: run-it
    description: Run the thing
    toolName: system.run
    argMode: freeform
requires:
  bins:
    - curl
    - https://example.com
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];
  EXPECT_EQ(s.name, "url-bins-skill");

  // If the URL was misclassified and caused a type_error exception, the
  // parse would be aborted and commands would be empty.
  ASSERT_EQ(s.commands.size(), 1u) << "Frontmatter parse aborted early — URL "
                                      "in bins likely caused type_error";
  EXPECT_EQ(s.commands[0].name, "run-it");

  // The URL must be stored as a string bin requirement (not dropped).
  ASSERT_EQ(s.required_bins.size(), 2u);
  EXPECT_EQ(s.required_bins[0], "curl");
  EXPECT_EQ(s.required_bins[1], "https://example.com");
}

TEST_F(SkillLoaderTest, TimeStringInEnvArrayParsedAsString) {
  // "09:00" must not be treated as object {09: "00"}.  The character after
  // the colon is a digit, which is not space/tab, so it stays a plain string.
  write_skill("time-env-skill", R"(---
name: time-env-skill
description: Skill whose env list contains time-like strings
always: true
commands:
  - name: check
    description: Check env
    toolName: system.run
    argMode: none
requires:
  env:
    - REQUIRED_VAR
    - 09:00
    - 12:30
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];

  // Same abort-on-exception signal as the URL test.
  ASSERT_EQ(s.commands.size(), 1u) << "Frontmatter parse aborted — time string "
                                      "in env likely caused type_error";
  EXPECT_EQ(s.commands[0].name, "check");

  ASSERT_EQ(s.required_envs.size(), 3u);
  EXPECT_EQ(s.required_envs[1], "09:00");
  EXPECT_EQ(s.required_envs[2], "12:30");
}

TEST_F(SkillLoaderTest, CommandsArrayWithMixedColonStrings) {
  // The commands array may appear alongside URL strings in other arrays.
  // Verify that only proper "name: value" entries become command objects and
  // URL-like strings in bins/env do not corrupt the command extraction.
  write_skill("mixed-colon-skill", R"(---
name: mixed-colon-skill
description: Mixed colon scenario
always: true
commands:
  - name: cmd-one
    description: First command
    toolName: system.run
    argMode: none
  - name: cmd-two
    description: Second command
    toolName: system.run
    argMode: freeform
requires:
  bins:
    - curl
    - https://api.example.com/v2
  env:
    - API_KEY
---

Content.
)");

  auto skills = skill_loader_->LoadSkillsFromDirectory(test_dir_);
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];
  EXPECT_EQ(s.name, "mixed-colon-skill");

  ASSERT_EQ(s.commands.size(), 2u);
  EXPECT_EQ(s.commands[0].name, "cmd-one");
  EXPECT_EQ(s.commands[1].name, "cmd-two");

  ASSERT_EQ(s.required_bins.size(), 2u);
  EXPECT_EQ(s.required_bins[1], "https://api.example.com/v2");

  ASSERT_EQ(s.required_envs.size(), 1u);
  EXPECT_EQ(s.required_envs[0], "API_KEY");
}
