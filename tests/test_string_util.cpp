// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/common/string_util.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

// ── Trim ─────────────────────────────────────────────────────────────────────

TEST(Trim, EmptyString) {
  EXPECT_EQ(Trim(""), "");
}
TEST(Trim, NoWhitespace) {
  EXPECT_EQ(Trim("hello"), "hello");
}
TEST(Trim, LeadingSpaces) {
  EXPECT_EQ(Trim("  hi"), "hi");
}
TEST(Trim, TrailingSpaces) {
  EXPECT_EQ(Trim("hi  "), "hi");
}
TEST(Trim, BothSides) {
  EXPECT_EQ(Trim("  hi  "), "hi");
}
TEST(Trim, TabsAndNewlines) {
  EXPECT_EQ(Trim("\t\nok\r\n"), "ok");
}
TEST(Trim, OnlyWhitespace) {
  EXPECT_EQ(Trim("   "), "");
}

TEST(TrimLeft, LeadingOnly) {
  EXPECT_EQ(TrimLeft("  ab  "), "ab  ");
}
TEST(TrimRight, TrailingOnly) {
  EXPECT_EQ(TrimRight("  ab  "), "  ab");
}

// ── Case conversion
// ───────────────────────────────────────────────────────────

TEST(ToLower, MixedCase) {
  EXPECT_EQ(ToLower("Hello World"), "hello world");
}
TEST(ToLower, AlreadyLower) {
  EXPECT_EQ(ToLower("abc"), "abc");
}
TEST(ToLower, Empty) {
  EXPECT_EQ(ToLower(""), "");
}
TEST(ToUpper, MixedCase) {
  EXPECT_EQ(ToUpper("Hello"), "HELLO");
}

TEST(Iequals, SameCase) {
  EXPECT_TRUE(Iequals("abc", "abc"));
}
TEST(Iequals, DifferentCase) {
  EXPECT_TRUE(Iequals("ABC", "abc"));
}
TEST(Iequals, DifferentLen) {
  EXPECT_FALSE(Iequals("ab", "abc"));
}
TEST(Iequals, Empty) {
  EXPECT_TRUE(Iequals("", ""));
}

// ── Prefix / Suffix
// ───────────────────────────────────────────────────────────

TEST(StartsWith, Match) {
  EXPECT_TRUE(StartsWith("foobar", "foo"));
}
TEST(StartsWith, NoMatch) {
  EXPECT_FALSE(StartsWith("foobar", "bar"));
}
TEST(StartsWith, Empty) {
  EXPECT_TRUE(StartsWith("abc", ""));
}
TEST(StartsWith, TooLong) {
  EXPECT_FALSE(StartsWith("ab", "abc"));
}

TEST(EndsWith, Match) {
  EXPECT_TRUE(EndsWith("foobar", "bar"));
}
TEST(EndsWith, NoMatch) {
  EXPECT_FALSE(EndsWith("foobar", "foo"));
}
TEST(EndsWith, Empty) {
  EXPECT_TRUE(EndsWith("abc", ""));
}

// ── Split
// ─────────────────────────────────────────────────────────────────────

TEST(Split, Basic) {
  auto v = Split("a,b,c", ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(Split, EmptyTokensPreserved) {
  auto v = Split("a,,b", ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[1], "");
}

TEST(Split, NoDelimiter) {
  auto v = Split("hello", ',');
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "hello");
}

TEST(Split, EmptyInput) {
  auto v = Split("", ',');
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "");
}

TEST(Split, SessionKey) {
  // Reproduces the session key split pattern in session_manager.cpp
  auto parts = Split("user:session:123", ':');
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[0], "user");
  EXPECT_EQ(parts[2], "123");
}

// ── Join ─────────────────────────────────────────────────────────────────────

TEST(Join, Basic) {
  std::vector<std::string> v = {"a", "b", "c"};
  EXPECT_EQ(Join(v, ", "), "a, b, c");
}

TEST(Join, SingleElement) {
  std::vector<std::string> v = {"only"};
  EXPECT_EQ(Join(v, ", "), "only");
}

TEST(Join, Empty) {
  std::vector<std::string> v;
  EXPECT_EQ(Join(v, ", "), "");
}

TEST(JoinWith, IntTransform) {
  std::vector<int> nums = {1, 2, 3};
  auto s = JoinWith(nums, " | ", [](int x) { return std::to_string(x); });
  EXPECT_EQ(s, "1 | 2 | 3");
}
