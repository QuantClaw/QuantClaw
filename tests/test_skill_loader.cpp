// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

// ============================================================================
// Skill 模块测试套件
//
// 测试范围：
//   - SkillLoaderFull: 完整加载 SKILL.md（YAML frontmatter + Markdown body）
//   - SkillLoaderMeta: 仅加载元数据（YAML frontmatter），用于快速发现
//   - SkillMetaTool: 技能元工具，实现渐进式披露和双上下文注入
//
// 数据结构：
//   - SkillMiniMetadata: 轻量级字段（name, description, emoji, always, os_restrict）
//   - SkillExtraMetadata: 扩展字段（required_bins, required_envs, installs, commands）
//   - SkillContent: Markdown 正文内容
//   - SkillFullData = SkillMiniMetadata + SkillExtraMetadata + SkillContent
//
// 测试分类：
//   1. 基础加载测试 - 简单技能加载、默认值、多技能加载
//   2. Gating 机制测试 - 环境变量、二进制文件、OS 限制、always 标志
//   3. 元数据字段测试 - emoji, primaryEnv, context 输出
//   4. 多目录加载测试 - 跨目录加载、去重、配置过滤
//   5. OpenClaw 兼容性测试 - requires 格式、install 格式、metadata 字段
//   6. Search Skill 测试 - 真实场景示例验证
//   7. 边界情况测试 - URL/时间字符串解析、冒号处理
//   8. SkillLoaderMeta 测试 - 元数据增删改查、上下文合并
//   9. SkillMetaTool 测试 - 渐进式披露、技能调用、双上下文注入
// ============================================================================

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

#ifdef _WIN32
#define test_setenv(name, value) _putenv_s(name, value)
#define test_unsetenv(name) _putenv_s(name, "")
#else
#define test_setenv(name, value) setenv(name, value, 1)
#define test_unsetenv(name) unsetenv(name)
#endif

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "quantclaw/config.hpp"
#include "quantclaw/skill/skill_loader_full.hpp"
#include "quantclaw/skill/skill_loader_meta.hpp"
#include "quantclaw/skill/skill_meta_tool.hpp"

#include "test_helpers.hpp"
#include <gtest/gtest.h>

// ============================================================================
// Test Fixture
// ============================================================================

class SkillLoaderFullTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Save original HOME environment variable
    auto get_or_empty = [](const char* name) -> std::string {
      const char* v = std::getenv(name);
      return v ? v : "";
    };
    old_home_ = get_or_empty("HOME");

    // Create isolated test directory
    test_dir_ = quantclaw::test::MakeTestDir("autotest-quantclaw-skills-test");

    // Create a fake home directory to isolate from real ~/.quantclaw/skills
    fake_home_ = quantclaw::test::MakeTestDir("autotest-quantclaw-fake-home");
    test_setenv("HOME", fake_home_.string().c_str());

    // Use console logger for debugging (can switch back to null_sink later)
    logger_ = spdlog::default_logger();

    skill_loader_ = std::make_unique<quantclaw::SkillLoaderFull>(logger_);
  }

  void TearDown() override {
    // Cleanup test directories
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
    if (std::filesystem::exists(fake_home_)) {
      std::filesystem::remove_all(fake_home_);
    }

    // Restore original HOME environment variable
    if (!old_home_.empty()) {
      test_setenv("HOME", old_home_.c_str());
    } else {
      test_unsetenv("HOME");
    }
  }

  // Helper: write a SKILL.md file in test_dir_/name/SKILL.md
  void write_skill(const std::string& name, const std::string& content) {
    auto dir = test_dir_ / "skills" / name;
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "SKILL.md", std::ios::binary);
    f << content;
    f.close();
  }

  // Helper: load skills from test_dir_ using public API
  std::vector<quantclaw::SkillFullData> load_skills() {
    quantclaw::SkillsConfig config;
    return skill_loader_->LoaderMultipleFullData(config, test_dir_);
  }

  std::filesystem::path test_dir_;
  std::filesystem::path fake_home_;
  std::string old_home_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SkillLoaderFull> skill_loader_;
};

// ============================================================================
// 1. Basic Loading Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, LoadSimpleSkill) {
  // 验证最基本的技能加载：name、description 和 body content 都能正确解析
  write_skill("test-skill", R"(---
name: test-skill
description: A simple test skill
---

# Test Skill

This is a test skill for QuantClaw.
)");

  auto skills = load_skills();

  ASSERT_EQ(skills.size(), 1u);
  // mini fields
  EXPECT_EQ(skills[0].mini.name, "test-skill");
  EXPECT_EQ(skills[0].mini.description, "A simple test skill");
  // body content
  EXPECT_TRUE(skills[0].body.content.find("This is a test skill") !=
              std::string::npos);
}

TEST_F(SkillLoaderFullTest, SkillWithNoRequirements) {
  // 验证没有 requires 字段的技能，所有 gating 相关字段应为空
  write_skill("simple", R"(---
name: simple
description: No requirements
---

Content here.
)");

  auto skills = load_skills();

  ASSERT_EQ(skills.size(), 1u);
  // extra fields (all empty)
  EXPECT_EQ(skills[0].extra.required_bins.size(), 0u);
  EXPECT_EQ(skills[0].extra.required_envs.size(), 0u);
  EXPECT_EQ(skills[0].extra.any_bins.size(), 0u);
  EXPECT_EQ(skills[0].extra.config_files.size(), 0u);
  // mini fields
  EXPECT_EQ(skills[0].mini.os_restrict.size(), 0u);
  EXPECT_FALSE(skills[0].mini.always);
}

TEST_F(SkillLoaderFullTest, MultipleSkills) {
  // 验证一次能加载多个技能文件
  write_skill("skill-a", "---\nname: skill-a\ndescription: A\n---\nA content.");
  write_skill("skill-b", "---\nname: skill-b\ndescription: B\n---\nB content.");

  auto skills = load_skills();
  EXPECT_EQ(skills.size(), 2u);
}

TEST_F(SkillLoaderFullTest, NonExistentDirectory) {
  // 验证加载不存在的目录时返回空列表而非报错
  quantclaw::SkillsConfig config;
  auto skills = skill_loader_->LoaderMultipleFullData(
      config, test_dir_ / "nonexistent");
  EXPECT_EQ(skills.size(), 0u);
}

TEST_F(SkillLoaderFullTest, SkillDefaultsNameFromDirectory) {
  // 验证 YAML 中没有 name 字段时，自动使用目录名作为技能名
  write_skill("dir-name", R"(---
description: No name field
---
Content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.name, "dir-name");
}

// ============================================================================
// 2. Gating & Requirements Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, SkillGatedByMissingEnv) {
  // 验证缺少必需环境变量时技能不会被加载
  write_skill("weather", R"(---
name: weather
description: Weather skill
requires:
  env:
    - WEATHER_API_KEY
---

Weather content.
)");

  test_unsetenv("WEATHER_API_KEY");
  auto skills = load_skills();
  EXPECT_EQ(skills.size(), 0u);
}

TEST_F(SkillLoaderFullTest, SkillWithAlwaysFlag) {
  // 验证 always: true 的技能无视 gating 条件，始终加载
  write_skill("always-on", R"(---
name: always-on
description: Always loaded
always: true
---

Always available.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.name, "always-on");
  EXPECT_TRUE(skills[0].mini.always);
}

TEST_F(SkillLoaderFullTest, SkillWithOsRestriction) {
  // 验证 OS 限制功能：非目标系统上技能不应加载
  write_skill("os-skill", R"(---
name: os-skill
description: Linux only
os:
  - linux
---

Linux only content.
)");

  auto skills = load_skills();

#ifdef __linux__
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.name, "os-skill");
#else
  EXPECT_EQ(skills.size(), 0u);
#endif
}

TEST_F(SkillLoaderFullTest, MacosAliasForDarwin) {
  // 验证 "macos" 是 "darwin" 的别名，在 macOS 上应该正常加载
  write_skill("macos-skill", R"(---
name: macos-skill
description: macOS only
os:
  - macos
---

macOS content.
)");

  auto skills = load_skills();

#if defined(__APPLE__) || defined(_WIN32)
  // macos alias should map to darwin on macOS, or pass on Windows
  ASSERT_EQ(skills.size(), 1u);
#else
  EXPECT_EQ(skills.size(), 0u);
#endif
}

// ============================================================================
// 3. Metadata Fields Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, SkillWithEmoji) {
  // 验证 emoji 字段能正确解析和保存
  write_skill("emoji-skill", R"(---
name: emoji-skill
description: Has emoji
emoji: "rocket"
---

Emoji content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.emoji, "rocket");
}

TEST_F(SkillLoaderFullTest, SkillWithPrimaryEnv) {
  // 验证 primaryEnv 字段能正确识别主要环境变量
  write_skill("env-skill", R"(---
name: env-skill
description: Has primary env
primaryEnv: MY_KEY
---

Env content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].extra.primary_env, "MY_KEY");
}

TEST_F(SkillLoaderFullTest, SkillContextOutput) {
  // 验证 GetSkillContext 能生成包含技能信息的上下文字符串
  write_skill("ctx-skill", R"(---
name: ctx-skill
description: Context test
requires:
  bins:
    - curl
---

star content
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);

  std::string context = skill_loader_->GetSkillContext(skills);
  EXPECT_TRUE(context.find("ctx-skill") != std::string::npos);
  EXPECT_TRUE(context.find("star") != std::string::npos);
}

// ============================================================================
// 4. Multi-Directory Loading Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, LoadSkillsMultiDir) {
  // 验证能从多个目录加载技能，包括主目录和 extra_dirs
  // Create two separate directories with different skills
  auto dir_a = quantclaw::test::MakeTestDir("autotest-quantclaw-multi-a");
  auto dir_b = quantclaw::test::MakeTestDir("autotest-quantclaw-multi-b");
  std::filesystem::create_directories(dir_a / "skills" / "autotest-skill-a");
  std::filesystem::create_directories(dir_b / "skills" / "autotest-skill-b");

  std::ofstream(dir_a / "skills" / "autotest-skill-a" / "SKILL.md")
      << "---\nname: autotest-skill-a\n---\nA";
  std::ofstream(dir_b / "skills" / "autotest-skill-b" / "SKILL.md")
      << "---\nname: autotest-skill-b\n---\nB";

  quantclaw::SkillsConfig config;
  config.load.extra_dirs = {(dir_b / "skills").string()};

  auto skills = skill_loader_->LoaderMultipleFullData(
      config, dir_a);

  bool found_a = false, found_b = false;
  for (const auto& s : skills) {
    if (s.mini.name == "autotest-skill-a")
      found_a = true;
    if (s.mini.name == "autotest-skill-b")
      found_b = true;
  }
  EXPECT_TRUE(found_a);
  EXPECT_TRUE(found_b);

  // Cleanup test directories
  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);
}

TEST_F(SkillLoaderFullTest, DeduplicationWorkspaceWins) {
  // 验证同名技能去重时，workspace 版本优先于 extra_dirs 版本
  // Create isolated test directories
  auto workspace = quantclaw::test::MakeTestDir("autotest-quantclaw-dedup-ws");
  auto extra_dir = quantclaw::test::MakeTestDir("autotest-quantclaw-dedup-extra");
  
  // Create workspace skill
  std::filesystem::create_directories(workspace / "skills" / "autotest-dupe-skill");
  std::ofstream(workspace / "skills" / "autotest-dupe-skill" / "SKILL.md")
      << "---\nname: autotest-dupe-skill\ndescription: workspace version\n---\nWS";
  
  // Create extra dir skill with same name
  std::filesystem::create_directories(extra_dir / "skills" / "autotest-dupe-skill");
  std::ofstream(extra_dir / "skills" / "autotest-dupe-skill" / "SKILL.md")
      << "---\nname: autotest-dupe-skill\ndescription: extra version\n---\nEXTRA";

  quantclaw::SkillsConfig config;
  config.load.extra_dirs = {(extra_dir / "skills").string()};

  auto skills = skill_loader_->LoaderMultipleFullData(
      config, workspace);

  // Should only have one instance, from workspace (first wins)
  int count = 0;
  for (const auto& s : skills) {
    if (s.mini.name == "autotest-dupe-skill") {
      ++count;
      EXPECT_EQ(s.mini.description, "workspace version")
          << "Workspace skill should win in deduplication";
    }
  }
  EXPECT_EQ(count, 1) << "Should have exactly one autotest-dupe-skill";

  // Cleanup test directories
  std::filesystem::remove_all(workspace);
  std::filesystem::remove_all(extra_dir);
}

TEST_F(SkillLoaderFullTest, PerSkillDisableViaConfig) {
  // 验证可以通过 config 配置禁用特定技能
  write_skill("enabled-skill",
              "---\nname: enabled-skill\ndescription: E\n---\nE.");
  write_skill("disabled-skill",
              "---\nname: disabled-skill\ndescription: D\n---\nD.");

  quantclaw::SkillsConfig config;
  config.entries["disabled-skill"].enabled = false;

  auto skills = skill_loader_->LoaderMultipleFullData(config, test_dir_);

  bool found_enabled = false, found_disabled = false;
  for (const auto& s : skills) {
    if (s.mini.name == "enabled-skill")
      found_enabled = true;
    if (s.mini.name == "disabled-skill")
      found_disabled = true;
  }
  EXPECT_TRUE(found_enabled);
  EXPECT_FALSE(found_disabled);
}

TEST_F(SkillLoaderFullTest, LoadSkillsUsesPlatformHomeForUserSkillDirectory) {
  // 验证能从用户目录 (~/.quantclaw/skills) 加载技能
  // This test verifies that LoaderMultipleFullData uses platform::home_directory()
  // to locate the user skill directory (~/.quantclaw/skills).
  // Note: The fixture already isolates HOME, so we work within that isolated environment.

  // Create user skill in the isolated fake home directory
  auto user_skills_dir = fake_home_ / ".quantclaw" / "skills";
  std::filesystem::create_directories(user_skills_dir / "autotest-user-skill");
  std::ofstream(user_skills_dir / "autotest-user-skill" / "SKILL.md")
      << "---\nname: autotest-user-skill\ndescription: User skill for testing\n---\nUser skill content";

  // Load skills - should pick up user-skill from fake home
  quantclaw::SkillsConfig config;
  auto skills =
      skill_loader_->LoaderMultipleFullData(config, test_dir_ / "workspace");

  // Verify user skill was loaded
  bool found = false;
  for (const auto& s : skills) {
    if (s.mini.name == "autotest-user-skill") {
      found = true;
      EXPECT_EQ(s.mini.description, "User skill for testing");
      break;
    }
  }
  EXPECT_TRUE(found) << "autotest-user-skill should be loaded from fake home directory";
}

// ============================================================================
// 5. OpenClaw Compatibility Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, NestedOpenClawRequiresFormat) {
  // 验证 OpenClaw 的嵌套 requires 格式（bins + env）能正确解析
  write_skill("nested-skill", R"(---
name: nested-skill
description: Nested requires
requires:
  bins:
    - curl
  env:
    - API_KEY
---

Nested content.
)");

  test_setenv("API_KEY", "value");
  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].extra.required_bins.size(), 1u);
  EXPECT_EQ(skills[0].extra.required_envs.size(), 1u);
  test_unsetenv("API_KEY");
}

TEST_F(SkillLoaderFullTest, NestedOpenClawWithAlwaysFlag) {
  // 验证 always: true 的技能即使 requires 不满足也能加载
  write_skill("nested-always", R"(---
name: nested-always
description: Nested with always
always: true
requires:
  bins:
    - nonexistent-binary
---

Always loads despite requirements.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.name, "nested-always");
  EXPECT_TRUE(skills[0].mini.always);
}

TEST_F(SkillLoaderFullTest, OpenClawInstallArrayFormat) {
  // 验证 OpenClaw 的 install 数组格式和 QuantClaw 对象格式都能解析
  // Test install array parsing with simplified format
  // Note: Full array-of-objects parsing depends on YAML parser capabilities
  write_skill("autotest-weather", R"(---
name: autotest-weather
description: Weather
emoji: "⛅"
always: true
homepage: "https://weather.example.com"
skillKey: "weather-v2"
requires:
  bins:
    - curl
install:
  brew: curl
  node: weather-cli
---

Weather skill.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  EXPECT_EQ(s.mini.name, "autotest-weather");
  EXPECT_EQ(s.mini.emoji, "⛅");
  EXPECT_EQ(s.extra.homepage, "https://weather.example.com");
  EXPECT_EQ(s.extra.skill_key, "weather-v2");
  ASSERT_EQ(s.extra.required_bins.size(), 1);
  EXPECT_EQ(s.extra.required_bins[0], "curl");

  // QuantClaw object format: {brew: curl, node: weather-cli}
  ASSERT_EQ(s.extra.installs.size(), 2);

  // Both install methods should parse correctly
  bool found_brew = false, found_node = false;
  for (const auto& inst : s.extra.installs) {
    if (inst.EffectiveMethod() == "brew") {
      EXPECT_EQ(inst.EffectiveFormula(), "curl");
      found_brew = true;
    }
    if (inst.EffectiveMethod() == "node") {
      EXPECT_EQ(inst.EffectiveFormula(), "weather-cli");
      found_node = true;
    }
  }
  EXPECT_TRUE(found_brew);
  EXPECT_TRUE(found_node);
}

TEST_F(SkillLoaderFullTest, QuantClawObjectInstallFormat) {
  // 验证 QuantClaw 对象格式的 install 字段（key=method, value=formula）
  // QuantClaw object format: each key is a method, value is formula or object
  write_skill("tools", R"(---
name: tools
install:
  apt: build-essential
  node: "@quantclaw/cli"
commands:
  - name: build
    tool: exec
    arg_mode: freeform
---

Tools content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  ASSERT_EQ(s.extra.installs.size(), 2);
  // Both formats should produce valid EffectiveMethod/Formula
  bool found_apt = false, found_node = false;
  for (const auto& inst : s.extra.installs) {
    if (inst.EffectiveMethod() == "apt") {
      EXPECT_EQ(inst.EffectiveFormula(), "build-essential");
      found_apt = true;
    }
    if (inst.EffectiveMethod() == "node") {
      EXPECT_EQ(inst.EffectiveFormula(), "@quantclaw/cli");
      found_node = true;
    }
  }
  EXPECT_TRUE(found_apt);
  EXPECT_TRUE(found_node);
}

TEST_F(SkillLoaderFullTest, OpenClawMetadataFieldsFallback) {
  // 验证 metadata.openclaw 字段能回退到顶层字段
  // Metadata.openclaw fields should populate top-level fields
  write_skill("meta-test", R"YAML(---
name: meta-test
description: Test
metadata:
  openclaw:
    emoji: "🔧"
    primaryEnv: MY_API_KEY
    always: true
    os:
      - linux
      - darwin
---

Content.
)YAML");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  EXPECT_EQ(s.mini.emoji, "🔧");
  EXPECT_EQ(s.extra.primary_env, "MY_API_KEY");
  EXPECT_TRUE(s.mini.always);
  ASSERT_EQ(s.mini.os_restrict.size(), 2);
  EXPECT_EQ(s.mini.os_restrict[0], "linux");
  EXPECT_EQ(s.mini.os_restrict[1], "darwin");
}

TEST_F(SkillLoaderFullTest, FlatFieldsNotOverriddenByMetadata) {
  // 验证顶层字段优先级高于 metadata.openclaw 中的同名字段
  // Top-level fields should take precedence over metadata.openclaw
  write_skill("priority-test", R"YAML(---
name: priority-test
emoji: "🎯"
primaryEnv: TOP_LEVEL
metadata:
  openclaw:
    emoji: "should-not-override"
    primaryEnv: should-not-override
---

Content.
)YAML");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1);
  auto& s = skills[0];

  // Top-level fields win
  EXPECT_EQ(s.mini.emoji, "🎯");
  EXPECT_EQ(s.extra.primary_env, "TOP_LEVEL");
}

TEST_F(SkillLoaderFullTest, InstallInfoEffectiveMethods) {
  // 验证 SkillInstallInfo 的 EffectiveMethod/Formula/Binary 优先级逻辑
  quantclaw::SkillInstallInfo info;

  // Empty defaults
  EXPECT_EQ(info.EffectiveMethod(), "");
  EXPECT_EQ(info.EffectiveFormula(), "");
  EXPECT_EQ(info.EffectiveBinary(), "");

  // method vs kind
  info.method = "npm";
  EXPECT_EQ(info.EffectiveMethod(), "npm");
  info.kind = "node";
  EXPECT_EQ(info.EffectiveMethod(), "node");

  // formula vs package vs module vs url
  info.formula = "pkg1";
  EXPECT_EQ(info.EffectiveFormula(), "pkg1");
  info.package = "pkg2";
  EXPECT_EQ(info.EffectiveFormula(), "pkg1");  // formula wins
  info.formula.clear();
  EXPECT_EQ(info.EffectiveFormula(), "pkg2");  // package wins
  info.package.clear();
  info.module = "mod1";
  EXPECT_EQ(info.EffectiveFormula(), "mod1");
  info.module.clear();
  info.url = "http://example.com";
  EXPECT_EQ(info.EffectiveFormula(), "http://example.com");

  // binary vs bins
  info.binary = "my-bin";
  EXPECT_EQ(info.EffectiveBinary(), "my-bin");
  info.bins = {"bin1", "bin2"};
  EXPECT_EQ(info.EffectiveBinary(), "my-bin");  // binary wins
  info.binary.clear();
  EXPECT_EQ(info.EffectiveBinary(), "bin1");  // first bin wins
  info.bins.clear();
  info.binary = "explicit-bin";
  EXPECT_EQ(info.EffectiveBinary(), "explicit-bin");
}

TEST_F(SkillLoaderFullTest, HomepageAndSkillKeyFromTopLevel) {
  // 验证 homepage 和 skillKey 字段能从顶层正确解析
  write_skill("topfields", R"(---
name: topfields
homepage: "https://example.com"
skillKey: my-key
---

Content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1);
  EXPECT_EQ(skills[0].extra.homepage, "https://example.com");
  EXPECT_EQ(skills[0].extra.skill_key, "my-key");
}

// ============================================================================
// 6. Search Skill Tests (Real-world Example)
// ============================================================================

TEST_F(SkillLoaderFullTest, SearchSkillAlwaysLoaded) {
  // 验证 always:true 的技能无需配置即可加载
  // always:true means the skill loads regardless of env/binary availability.
  write_skill("search", R"(---
name: search
description: Web search with multiple providers
always: true
emoji: "🔍"
---

# Search

Search the web using multiple providers.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_EQ(skills[0].mini.name, "search");
  EXPECT_TRUE(skills[0].mini.always);
}

TEST_F(SkillLoaderFullTest, SearchSkillHasSearchCommand) {
  // 验证 search 技能的 commands 数组能正确解析
  write_skill("search", R"(---
name: search
always: true
commands:
  - name: search
    description: Search the web
    tool: web_search
    arg_mode: freeform
---

Search content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);

  const auto& cmds = skills[0].extra.commands;
  ASSERT_EQ(cmds.size(), 1u);
  EXPECT_EQ(cmds[0].name, "search");
  EXPECT_EQ(cmds[0].tool_name, "web_search");
  EXPECT_EQ(cmds[0].arg_mode, "freeform");
}

TEST_F(SkillLoaderFullTest, SearchSkillContentMentionsProviders) {
  // 验证技能内容中包含搜索引擎提供商信息
  write_skill("search", R"(---
name: search
always: true
---

# Web Search

Supports web_search via Tavily, Brave, and DuckDuckGo.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_NE(skills[0].body.content.find("web_search"), std::string::npos);
  EXPECT_NE(skills[0].body.content.find("Tavily"), std::string::npos);
  EXPECT_NE(skills[0].body.content.find("DuckDuckGo"), std::string::npos);
}

TEST_F(SkillLoaderFullTest, SearchSkillNoRequiredBinsOrEnvs) {
  // 验证 DuckDuckGo 回退机制使 search 技能无需硬性 gating
  // DuckDuckGo fallback requires no API key, so no hard gating.
  write_skill("search", R"(---
name: search
always: true
---

No requirements.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  EXPECT_TRUE(skills[0].extra.required_bins.empty());
  EXPECT_TRUE(skills[0].extra.required_envs.empty());
}

TEST_F(SkillLoaderFullTest, SearchSkillGetAllCommandsIncludesSearch) {
  // 验证 GetAllCommands 能提取技能中定义的斜杠命令
  write_skill("search", R"(---
name: search
always: true
commands:
  - name: search
    tool: web_search
    arg_mode: freeform
---

Search.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);

  auto all_cmds = skill_loader_->GetAllCommands(skills);
  ASSERT_EQ(all_cmds.size(), 1u);
  EXPECT_EQ(all_cmds[0].name, "search");
  EXPECT_EQ(all_cmds[0].tool_name, "web_search");
}

TEST_F(SkillLoaderFullTest, SearchSkillContextOutputContainsName) {
  // 验证 GetSkillContext 输出中包含技能名和 emoji
  write_skill("search", R"(---
name: search
emoji: "🔍"
always: true
---

Search the web.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);

  std::string context = skill_loader_->GetSkillContext(skills);
  EXPECT_NE(context.find("search"), std::string::npos);
  EXPECT_NE(context.find("🔍"), std::string::npos);
}

// ============================================================================
// 7. Edge Cases & Parsing Tests
// ============================================================================

TEST_F(SkillLoaderFullTest, UrlInBinsArrayParsedAsString) {
  // 验证 bins 数组中的 URL 字符串不会被错误解析为对象，避免中断整个解析流程
  // A URL in the bins: array must remain a plain string.  Without the fix,
  // "https://example.com" is parsed as object {https: "//example.com"},
  // causing get<vector<string>>() to throw and aborting the whole parse —
  // leaving commands empty even if they were defined first.
  write_skill("url-bins-skill", R"(---
name: url-bins-skill
always: true
requires:
  bins:
    - curl
    - https://example.com
commands:
  - name: run-it
    tool: exec
    arg_mode: freeform
---

Content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];
  EXPECT_EQ(s.mini.name, "url-bins-skill");

  // If the URL was misclassified and caused a type_error exception, the
  // parse would be aborted and commands would be empty.
  ASSERT_EQ(s.extra.commands.size(), 1u) << "Frontmatter parse aborted early — URL "
                                      "in bins likely caused type_error";
  EXPECT_EQ(s.extra.commands[0].name, "run-it");

  // The URL must be stored as a string bin requirement (not dropped).
  ASSERT_EQ(s.extra.required_bins.size(), 2u);
  EXPECT_EQ(s.extra.required_bins[0], "curl");
  EXPECT_EQ(s.extra.required_bins[1], "https://example.com");
}

TEST_F(SkillLoaderFullTest, TimeStringInEnvArrayParsedAsString) {
  // 验证 "09:00" 这类时间字符串不会被当作对象 {09: "00"} 解析
  // "09:00" must not be treated as object {09: "00"}.  The character after
  // the colon is a digit, which is not space/tab, so it stays a plain string.
  write_skill("time-env-skill", R"(---
name: time-env-skill
always: true
requires:
  env:
    - API_KEY
    - "09:00"
    - "12:30"
commands:
  - name: check
    tool: exec
    arg_mode: freeform
---

Content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];

  // Same abort-on-exception signal as the URL test.
  ASSERT_EQ(s.extra.commands.size(), 1u) << "Frontmatter parse aborted — time string "
                                      "in env likely caused type_error";
  EXPECT_EQ(s.extra.commands[0].name, "check");

  ASSERT_EQ(s.extra.required_envs.size(), 3u);
  EXPECT_EQ(s.extra.required_envs[1], "09:00");
  EXPECT_EQ(s.extra.required_envs[2], "12:30");
}

TEST_F(SkillLoaderFullTest, CommandsArrayWithMixedColonStrings) {
  // 验证 commands 数组与其他含冒号字符串共存时，只有正确的 "name: value" 会被解析为命令对象
  // The commands array may appear alongside URL strings in other arrays.
  // Verify that only proper "name: value" entries become command objects and
  // URL-like strings in bins/env do not corrupt the command extraction.
  write_skill("mixed-colon-skill", R"(---
name: mixed-colon-skill
always: true
requires:
  bins:
    - curl
    - https://api.example.com/v2
  env:
    - API_KEY
commands:
  - name: cmd-one
    tool: exec
    arg_mode: freeform
  - name: cmd-two
    tool: read
    arg_mode: none
---

Content.
)");

  auto skills = load_skills();
  ASSERT_EQ(skills.size(), 1u);
  const auto& s = skills[0];
  EXPECT_EQ(s.mini.name, "mixed-colon-skill");

  ASSERT_EQ(s.extra.commands.size(), 2u);
  EXPECT_EQ(s.extra.commands[0].name, "cmd-one");
  EXPECT_EQ(s.extra.commands[1].name, "cmd-two");

  ASSERT_EQ(s.extra.required_bins.size(), 2u);
  EXPECT_EQ(s.extra.required_bins[1], "https://api.example.com/v2");

  ASSERT_EQ(s.extra.required_envs.size(), 1u);
  EXPECT_EQ(s.extra.required_envs[0], "API_KEY");
}

// ============================================================================
// 8. SkillLoaderMeta Tests
// ============================================================================

class SkillLoaderMetaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 准备测试环境：隔离 HOME 目录，创建测试技能文件
    auto get_or_empty = [](const char* name) -> std::string {
      const char* v = std::getenv(name);
      return v ? v : "";
    };
    old_home_ = get_or_empty("HOME");
    test_dir_ = quantclaw::test::MakeTestDir("autotest-quantclaw-meta-test");
    fake_home_ = quantclaw::test::MakeTestDir("autotest-quantclaw-fake-home-meta");
    test_setenv("HOME", fake_home_.string().c_str());
    logger_ = spdlog::default_logger();
    meta_loader_ = std::make_unique<quantclaw::SkillLoaderMeta>(logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
    if (std::filesystem::exists(fake_home_)) {
      std::filesystem::remove_all(fake_home_);
    }
    if (!old_home_.empty()) {
      test_setenv("HOME", old_home_.c_str());
    } else {
      test_unsetenv("HOME");
    }
  }

  void write_skill(const std::string& name, const std::string& content) {
    auto dir = test_dir_ / "skills" / name;
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "SKILL.md", std::ios::binary);
    f << content;
    f.close();
  }

  std::filesystem::path test_dir_;
  std::filesystem::path fake_home_;
  std::string old_home_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SkillLoaderMeta> meta_loader_;
};

TEST_F(SkillLoaderMetaTest, LoadOneMetaData) {
  // 验证能从单个 SKILL.md 文件加载元数据（不读取 body）
  write_skill("test-meta", R"(---
name: test-meta
description: Meta test skill
emoji: "🧪"
---

Content here.
)");

  auto skill_file = (test_dir_ / "skills" / "test-meta" / "SKILL.md").string();
  auto meta = meta_loader_->LoaderOneMetaData(skill_file);

  EXPECT_EQ(meta.mini.name, "test-meta");
  EXPECT_EQ(meta.mini.description, "Meta test skill");
  EXPECT_EQ(meta.mini.emoji, "🧪");
  EXPECT_FALSE(meta.mini.always);
}

TEST_F(SkillLoaderMetaTest, LoadAllMetaData) {
  // 验证能从目录加载所有技能的元数据，并标记扫描完成
  write_skill("skill-a", "---\nname: skill-a\ndescription: A\n---\nA");
  write_skill("skill-b", "---\nname: skill-b\ndescription: B\nalways: true\n---\nB");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  ASSERT_EQ(metas.size(), 2u);
  EXPECT_TRUE(meta_loader_->IsLoadAllMetaData());
}

TEST_F(SkillLoaderMetaTest, AddAndRemoveMetaData) {
  // 验证能动态添加和删除元数据，并正确维护内部缓存
  write_skill("add-test", "---\nname: add-test\ndescription: Add test\n---\nContent");

  auto skill_file = (test_dir_ / "skills" / "add-test" / "SKILL.md").string();
  EXPECT_TRUE(meta_loader_->AddOneMetaData(skill_file));

  auto metas = meta_loader_->GetAllMetaData();
  ASSERT_EQ(metas.size(), 1u);
  EXPECT_EQ(metas[0].mini.name, "add-test");

  // Remove by name
  EXPECT_TRUE(meta_loader_->RemoveOneMetaData("add-test"));
  EXPECT_FALSE(meta_loader_->RemoveOneMetaData("nonexistent"));

  metas = meta_loader_->GetAllMetaData();
  EXPECT_EQ(metas.size(), 0u);
}

TEST_F(SkillLoaderMetaTest, RewriteOneMetaData) {
  // 验证能更新已存在的元数据，修改后字段应反映最新值
  write_skill("rewrite-test", "---\nname: rewrite-test\ndescription: Original\n---\nContent");

  auto skill_file = (test_dir_ / "skills" / "rewrite-test" / "SKILL.md").string();
  meta_loader_->AddOneMetaData(skill_file);

  // Modify the skill file
  write_skill("rewrite-test", "---\nname: rewrite-test\ndescription: Updated\nemoji: 🔄\n---\nContent");

  EXPECT_TRUE(meta_loader_->RewriteOneMetaData(skill_file));

  auto meta = meta_loader_->FindMetaDataByName("rewrite-test");
  ASSERT_TRUE(meta.has_value());
  EXPECT_EQ(meta->mini.description, "Updated");
  EXPECT_EQ(meta->mini.emoji, "🔄");
}

TEST_F(SkillLoaderMetaTest, FindMetaDataByNameAndKey) {
  // 验证能通过名称、skillKey、文件路径三种方式查找元数据
  write_skill("find-test", R"(---
name: find-test
description: Find test
skillKey: my-custom-key
---

Content
)");

  auto skill_file = (test_dir_ / "skills" / "find-test" / "SKILL.md").string();
  meta_loader_->AddOneMetaData(skill_file);

  // Find by name
  auto by_name = meta_loader_->FindMetaDataByName("find-test");
  ASSERT_TRUE(by_name.has_value());
  EXPECT_EQ(by_name->mini.name, "find-test");
  
  // Find by key
  auto by_key = meta_loader_->FindMetaDataByKey("my-custom-key");
  ASSERT_TRUE(by_key.has_value());
  EXPECT_EQ(by_key->extra.skill_key, "my-custom-key");
  
  // Find by file path
  auto by_file = meta_loader_->FindMetaDataByFile(skill_file);
  ASSERT_TRUE(by_file.has_value());
  EXPECT_EQ(by_file->mini.name, "find-test");
}

TEST_F(SkillLoaderMetaTest, MergeSkillContext) {
  // 验证能将多个技能元数据合并为格式化的上下文字符串
  write_skill("ctx-a", "---\nname: ctx-a\ndescription: Context A\nemoji: 🅰️\n---\nA");
  write_skill("ctx-b", "---\nname: ctx-b\ndescription: Context B\nemoji: 🅱️\n---\nB");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  std::string context = meta_loader_->MergeSkillContext(metas);
  EXPECT_TRUE(context.find("ctx-a") != std::string::npos);
  EXPECT_TRUE(context.find("ctx-b") != std::string::npos);
  EXPECT_TRUE(context.find("Context A") != std::string::npos);
  EXPECT_TRUE(context.find("Context B") != std::string::npos);
}

TEST_F(SkillLoaderMetaTest, MergeSkillContextWithEmptyList) {
  // 验证空列表的边界情况：不应崩溃或输出无效内容
  std::vector<quantclaw::SkillMetadata> empty_metas;
  std::string context = meta_loader_->MergeSkillContext(empty_metas);
  EXPECT_TRUE(context.empty() || context.find("Available skills") == std::string::npos);
}

// ============================================================================
// 9. SkillMetaTool Tests
// ============================================================================

class SkillMetaToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 准备测试环境：初始化 meta_loader 和 meta_tool
    auto get_or_empty = [](const char* name) -> std::string {
      const char* v = std::getenv(name);
      return v ? v : "";
    };
    old_home_ = get_or_empty("HOME");
    test_dir_ = quantclaw::test::MakeTestDir("autotest-quantclaw-metatool-test");
    fake_home_ = quantclaw::test::MakeTestDir("autotest-quantclaw-fake-home-metatool");
    test_setenv("HOME", fake_home_.string().c_str());
    logger_ = spdlog::default_logger();
    meta_loader_ = std::make_unique<quantclaw::SkillLoaderMeta>(logger_);
    meta_tool_ = std::make_unique<quantclaw::SkillMetaTool>(logger_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
    if (std::filesystem::exists(fake_home_)) {
      std::filesystem::remove_all(fake_home_);
    }
    if (!old_home_.empty()) {
      test_setenv("HOME", old_home_.c_str());
    } else {
      test_unsetenv("HOME");
    }
  }

  void write_skill(const std::string& name, const std::string& content) {
    auto dir = test_dir_ / "skills" / name;
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "SKILL.md", std::ios::binary);
    f << content;
    f.close();
  }

  std::filesystem::path test_dir_;
  std::filesystem::path fake_home_;
  std::string old_home_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unique_ptr<quantclaw::SkillLoaderMeta> meta_loader_;
  std::unique_ptr<quantclaw::SkillMetaTool> meta_tool_;
};

TEST_F(SkillMetaToolTest, BuildSchemaProgressiveDisclosure) {
  // 验证渐进式披露：schema 只包含技能名和描述，不包含完整内容
  write_skill("search", "---\nname: search\ndescription: Web search\nemoji: 🔍\n---\nSearch content");
  write_skill("weather", "---\nname: weather\ndescription: Weather info\nemoji: ⛅\n---\nWeather content");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  auto schema = meta_tool_->BuildSchema(metas);

  // Schema should have parameters with "command" field
  EXPECT_TRUE(schema.parameters.contains("properties"));
  EXPECT_TRUE(schema.parameters["properties"].contains("command"));
  EXPECT_TRUE(schema.parameters.contains("required"));

  // Description should list skill names and descriptions (progressive disclosure)
  EXPECT_TRUE(schema.description.find("search") != std::string::npos);
  EXPECT_TRUE(schema.description.find("weather") != std::string::npos);
  EXPECT_TRUE(schema.description.find("Web search") != std::string::npos);
  EXPECT_TRUE(schema.description.find("Weather info") != std::string::npos);

  // Full content should NOT be in description (progressive disclosure)
  EXPECT_TRUE(schema.description.find("Search content") == std::string::npos);
  EXPECT_TRUE(schema.description.find("Weather content") == std::string::npos);
}

TEST_F(SkillMetaToolTest, BuildSchemaWithEmptyMetadata) {
  // 验证空元数据列表时 schema 仍能正常生成
  std::vector<quantclaw::SkillMetadata> empty_metas;
  auto schema = meta_tool_->BuildSchema(empty_metas);

  EXPECT_FALSE(schema.description.empty());
  EXPECT_TRUE(schema.parameters.contains("properties"));
}

TEST_F(SkillMetaToolTest, HandleInvocationSuccess) {
  // 验证成功调用技能：返回 visible/hidden 消息和执行上下文
  write_skill("test-skill", R"(---
name: test-skill
description: Test skill
emoji: 🧪
---

This is the full skill content with detailed instructions.
)");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"command", "test-skill"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.skill_name, "test-skill");
  EXPECT_TRUE(result.visible_message.find("test-skill") != std::string::npos);
  EXPECT_TRUE(result.hidden_message.find("full skill content") != std::string::npos);
  EXPECT_TRUE(result.hidden_message.find("detailed instructions") != std::string::npos);
  EXPECT_TRUE(result.error.empty());
}

TEST_F(SkillMetaToolTest, HandleInvocationWithSkillKey) {
  // 验证能通过自定义 skillKey 而非技能名来调用
  write_skill("my-skill", R"(---
name: my-skill
description: Skill with custom key
skillKey: custom-key
---

Custom key skill content.
)");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  // Invoke by skill key instead of name
  nlohmann::json args = {{"command", "custom-key"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.skill_name, "my-skill");
}

TEST_F(SkillMetaToolTest, HandleInvocationMissingCommand) {
  // 验证缺少 command 参数时应返回错误
  write_skill("test-skill", "---\nname: test-skill\ndescription: Test\n---\nContent");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"other_field", "value"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.find("command") != std::string::npos);
}

TEST_F(SkillMetaToolTest, HandleInvocationSkillNotFound) {
  // 验证调用不存在的技能时应返回错误
  write_skill("test-skill", "---\nname: test-skill\ndescription: Test\n---\nContent");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"command", "nonexistent-skill"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error.find("nonexistent-skill") != std::string::npos);
}

TEST_F(SkillMetaToolTest, HandleInvocationIncludesExecutionContext) {
  // 验证技能调用能正确返回 allowed_tools 和 model_override
  write_skill("exec-ctx-skill", R"(---
name: exec-ctx-skill
description: Execution context test
allowed_tools:
  - exec
  - read
model_override: opus
---

Execution context skill content.
)");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"command", "exec-ctx-skill"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_TRUE(result.success);
  ASSERT_EQ(result.allowed_tools.size(), 2u);
  EXPECT_EQ(result.allowed_tools[0], "exec");
  EXPECT_EQ(result.allowed_tools[1], "read");
  EXPECT_EQ(result.model_override, "opus");
}

TEST_F(SkillMetaToolTest, VisibleMessageFormat) {
  // 验证 visible message 格式：<command-message> 标签和 emoji 处理
  write_skill("emoji-skill", "---\nname: emoji-skill\nemoji: 🚀\n---\nContent");
  write_skill("no-emoji-skill", "---\nname: no-emoji-skill\n---\nContent");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  auto result_with_emoji = meta_tool_->HandleInvocation({{"command", "emoji-skill"}}, metas);
  auto result_without_emoji = meta_tool_->HandleInvocation({{"command", "no-emoji-skill"}}, metas);

  EXPECT_TRUE(result_with_emoji.visible_message.find("🚀") != std::string::npos);
  EXPECT_TRUE(result_with_emoji.visible_message.find("<command-message>") != std::string::npos);
  EXPECT_TRUE(result_without_emoji.visible_message.find("<command-message>") != std::string::npos);
}

TEST_F(SkillMetaToolTest, HiddenMessageIncludesResourceDirs) {
  // 验证 hidden message 包含资源目录信息（scripts/references/assets）
  // Create skill with resource directories
  auto skill_dir = test_dir_ / "skills" / "resource-skill";
  std::filesystem::create_directories(skill_dir / "scripts");
  std::filesystem::create_directories(skill_dir / "references");
  std::filesystem::create_directories(skill_dir / "assets");

  std::ofstream(skill_dir / "SKILL.md") << "---\nname: resource-skill\n---\nResource skill content";

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"command", "resource-skill"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.hidden_message.find("Scripts:") != std::string::npos);
  EXPECT_TRUE(result.hidden_message.find("References:") != std::string::npos);
  EXPECT_TRUE(result.hidden_message.find("Assets:") != std::string::npos);
}

TEST_F(SkillMetaToolTest, HiddenMessageIncludesCommands) {
  // 验证 hidden message 包含技能定义的斜杠命令列表
  write_skill("cmd-skill", R"(---
name: cmd-skill
commands:
  - name: test-cmd
    description: Test command
    tool: exec
    arg_mode: freeform
---

Command skill content.
)");

  quantclaw::SkillsConfig config;
  auto metas = meta_loader_->LoaderAllMetaData(config, test_dir_);

  nlohmann::json args = {{"command", "cmd-skill"}};
  auto result = meta_tool_->HandleInvocation(args, metas);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.hidden_message.find("/test-cmd") != std::string::npos);
  EXPECT_TRUE(result.hidden_message.find("Test command") != std::string::npos);
}
