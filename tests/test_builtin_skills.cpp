// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <unordered_set>

#include "quantclaw/builtin_skills.hpp"

#include <gtest/gtest.h>

namespace quantclaw {
namespace {

// ── helpers ──────────────────────────────────────────────────────────────────

// Returns true when the SKILL.md content starts with a YAML frontmatter block:
// the opening "---" must be at position 0 (or after a leading newline at most),
// and a closing "---" must follow on a later line.  Bare "---" markdown
// horizontal rules elsewhere in the body do not trigger a false positive.
bool HasFrontmatter(const std::string& content) {
  // Opening delimiter must be at the very start of the file.
  if (content.rfind("---", 0) == std::string::npos)
    return false;
  // Closing delimiter must appear after the first newline past the opener.
  auto after_open = content.find('\n');
  if (after_open == std::string::npos)
    return false;
  return content.find("---", after_open) != std::string::npos;
}

// Returns true when the frontmatter contains "key: value" (value non-empty).
bool FrontmatterHas(const std::string& content, const std::string& key,
                    const std::string& value = "") {
  auto close = content.find("---", content.find("---") + 3);
  std::string fm = content.substr(0, close + 3);
  auto pos = fm.find(key + ":");
  if (pos == std::string::npos)
    return false;
  if (value.empty())
    return true;
  return fm.find(value, pos) != std::string::npos;
}

// ── fixture ──────────────────────────────────────────────────────────────────

class BuiltinSkillsTest : public ::testing::Test {
 protected:
  const std::vector<BuiltinSkill>& skills() {
    return GetBuiltinSkills();
  }

  // Returns the skill with the given name, or nullptr if not found.
  const BuiltinSkill* find(const std::string& name) {
    for (const auto& s : skills()) {
      if (s.name == name)
        return &s;
    }
    return nullptr;
  }
};

// ── basic sanity ─────────────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, NonEmpty) {
  EXPECT_FALSE(skills().empty());
}

TEST_F(BuiltinSkillsTest, Singleton) {
  // Must return the same static object on every call.
  EXPECT_EQ(&GetBuiltinSkills(), &GetBuiltinSkills());
}

TEST_F(BuiltinSkillsTest, AllFieldsNonNull) {
  for (const auto& s : skills()) {
    EXPECT_NE(s.name, nullptr) << "null name pointer";
    EXPECT_NE(s.content, nullptr)
        << "null content pointer for skill " << (s.name ? s.name : "?");
  }
}

TEST_F(BuiltinSkillsTest, AllNamesNonEmpty) {
  for (const auto& s : skills()) {
    EXPECT_STRNE(s.name, "") << "empty name";
  }
}

TEST_F(BuiltinSkillsTest, AllContentsNonEmpty) {
  for (const auto& s : skills()) {
    EXPECT_STRNE(s.content, "") << "empty content for " << s.name;
  }
}

TEST_F(BuiltinSkillsTest, AllNamesUnique) {
  std::unordered_set<std::string> seen;
  for (const auto& s : skills()) {
    EXPECT_TRUE(seen.insert(s.name).second)
        << "duplicate skill name: " << s.name;
  }
}

TEST_F(BuiltinSkillsTest, AllHaveFrontmatter) {
  for (const auto& s : skills()) {
    EXPECT_TRUE(HasFrontmatter(s.content))
        << s.name << ": missing YAML frontmatter";
  }
}

// ── known skills present ─────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, SearchSkillPresent) {
  EXPECT_NE(find("search"), nullptr);
}

TEST_F(BuiltinSkillsTest, WeatherSkillPresent) {
  EXPECT_NE(find("weather"), nullptr);
}

TEST_F(BuiltinSkillsTest, GithubSkillPresent) {
  EXPECT_NE(find("github"), nullptr);
}

TEST_F(BuiltinSkillsTest, HealthcheckSkillPresent) {
  EXPECT_NE(find("healthcheck"), nullptr);
}

TEST_F(BuiltinSkillsTest, SkillCreatorPresent) {
  EXPECT_NE(find("skill-creator"), nullptr);
}

// ── search skill ─────────────────────────────────────────────────────────────

class SearchSkillTest : public BuiltinSkillsTest {
 protected:
  void SetUp() override {
    skill_ = find("search");
    ASSERT_NE(skill_, nullptr) << "search skill must be registered";
    content_ = skill_->content;
  }
  const BuiltinSkill* skill_ = nullptr;
  std::string content_;
};

TEST_F(SearchSkillTest, HasAlwaysTrue) {
  EXPECT_TRUE(FrontmatterHas(content_, "always", "true"))
      << "search skill must have always: true so it loads without config";
}

TEST_F(SearchSkillTest, HasWebSearchTool) {
  EXPECT_NE(content_.find("web_search"), std::string::npos)
      << "search skill must reference the web_search tool";
}

TEST_F(SearchSkillTest, HasTavilyProvider) {
  EXPECT_NE(content_.find("Tavily"), std::string::npos)
      << "search skill must mention Tavily provider";
}

TEST_F(SearchSkillTest, HasDuckDuckGoProvider) {
  EXPECT_NE(content_.find("DuckDuckGo"), std::string::npos)
      << "search skill must mention DuckDuckGo fallback provider";
}

TEST_F(SearchSkillTest, HasSearchCommand) {
  // Check the commands block by asserting on toolName, which only appears
  // inside a command entry (not in the top-level frontmatter "name: search").
  EXPECT_NE(content_.find("toolName: web_search"), std::string::npos)
      << "search skill commands block must map to web_search tool";
}

TEST_F(SearchSkillTest, DocumentsQueryParameter) {
  EXPECT_NE(content_.find("query"), std::string::npos)
      << "search skill must document the query parameter";
}

TEST_F(SearchSkillTest, DocumentsCountParameter) {
  EXPECT_NE(content_.find("count"), std::string::npos)
      << "search skill must document the count parameter";
}

TEST_F(SearchSkillTest, NoRequiredBinaries) {
  // Search works via built-in tool; must not require external binaries.
  std::string fm;
  {
    auto first = content_.find("---");
    auto close = content_.find("---", first + 3);
    fm = content_.substr(0, close + 3);
  }
  EXPECT_EQ(fm.find("bins:"), std::string::npos)
      << "search skill must not require external binaries";
}

// ── weather skill ────────────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, WeatherHasAlwaysTrue) {
  const BuiltinSkill* s = find("weather");
  ASSERT_NE(s, nullptr);
  EXPECT_TRUE(FrontmatterHas(std::string(s->content), "always", "true"));
}

TEST_F(BuiltinSkillsTest, WeatherMentionsWttrIn) {
  const BuiltinSkill* s = find("weather");
  ASSERT_NE(s, nullptr);
  EXPECT_NE(std::string(s->content).find("wttr.in"), std::string::npos);
}

// ── github skill ─────────────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, GithubRequiresGhBinary) {
  const BuiltinSkill* s = find("github");
  ASSERT_NE(s, nullptr);
  std::string c = s->content;
  // Must declare a bins requirement for "gh".
  EXPECT_NE(c.find("bins:"), std::string::npos)
      << "github skill must declare required binaries";
  EXPECT_NE(c.find("- gh"), std::string::npos)
      << "github skill must require the gh binary";
}

TEST_F(BuiltinSkillsTest, GithubMentionsGhCli) {
  const BuiltinSkill* s = find("github");
  ASSERT_NE(s, nullptr);
  EXPECT_NE(std::string(s->content).find("gh"), std::string::npos);
}

// ── healthcheck skill
// ─────────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, HealthcheckHasAlwaysTrue) {
  const BuiltinSkill* s = find("healthcheck");
  ASSERT_NE(s, nullptr);
  EXPECT_TRUE(FrontmatterHas(std::string(s->content), "always", "true"));
}

TEST_F(BuiltinSkillsTest, HealthcheckHasCommand) {
  const BuiltinSkill* s = find("healthcheck");
  ASSERT_NE(s, nullptr);
  std::string c = s->content;
  // "name: healthcheck" also appears in the frontmatter, so assert on
  // argMode: none which is only present inside the commands block.
  EXPECT_NE(c.find("argMode: none"), std::string::npos)
      << "healthcheck commands block must declare argMode: none";
  // Confirm the command routes to the correct tool.
  EXPECT_NE(c.find("toolName: system.run"), std::string::npos)
      << "healthcheck command must reference system.run tool";
}

// ── skill-creator skill
// ───────────────────────────────────────────────────────

TEST_F(BuiltinSkillsTest, SkillCreatorHasAlwaysTrue) {
  const BuiltinSkill* s = find("skill-creator");
  ASSERT_NE(s, nullptr);
  EXPECT_TRUE(FrontmatterHas(std::string(s->content), "always", "true"));
}

TEST_F(BuiltinSkillsTest, SkillCreatorDocumentsFrontmatterFormat) {
  const BuiltinSkill* s = find("skill-creator");
  ASSERT_NE(s, nullptr);
  std::string c = s->content;
  EXPECT_NE(c.find("SKILL.md"), std::string::npos)
      << "skill-creator must document SKILL.md structure";
  EXPECT_NE(c.find("description:"), std::string::npos)
      << "skill-creator must show the description field";
}

}  // namespace
}  // namespace quantclaw
