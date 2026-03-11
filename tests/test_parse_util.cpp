// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/common/parse_util.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

// ── ParseInt ─────────────────────────────────────────────────────────────────

TEST(ParseInt, ValidInteger) {
  EXPECT_EQ(ParseInt("42"), 42);
}
TEST(ParseInt, Zero) {
  EXPECT_EQ(ParseInt("0"), 0);
}
TEST(ParseInt, Negative) {
  EXPECT_EQ(ParseInt("-7"), -7);
}
TEST(ParseInt, EmptyIsNullopt) {
  EXPECT_FALSE(ParseInt("").has_value());
}
TEST(ParseInt, NonNumeric) {
  EXPECT_FALSE(ParseInt("abc").has_value());
}
TEST(ParseInt, TrailingGarbage) {
  EXPECT_FALSE(ParseInt("42x").has_value());
}
TEST(ParseInt, LeadingWhitespace) {
  EXPECT_FALSE(ParseInt(" 42").has_value());
}  // no implicit trim
TEST(ParseInt, FloatIsRejected) {
  EXPECT_FALSE(ParseInt("3.14").has_value());
}

TEST(ParseInt, RangeMin) {
  EXPECT_FALSE(ParseInt("0", 1, 100).has_value());
}
TEST(ParseInt, RangeMax) {
  EXPECT_FALSE(ParseInt("101", 1, 100).has_value());
}
TEST(ParseInt, RangeExact) {
  EXPECT_EQ(ParseInt("50", 1, 100), 50);
}
TEST(ParseInt, RangeBoundaryMin) {
  EXPECT_EQ(ParseInt("1", 1, 100), 1);
}
TEST(ParseInt, RangeBoundaryMax) {
  EXPECT_EQ(ParseInt("100", 1, 100), 100);
}

// uint16_t overflow (65536 > uint16_t max)
TEST(ParseInt, Uint16Overflow) {
  EXPECT_FALSE(ParseInt<uint16_t>("65536").has_value());
}

// ── ParsePort ────────────────────────────────────────────────────────────────

TEST(ParsePort, ValidPort) {
  EXPECT_EQ(ParsePort("8080"), 8080);
}
TEST(ParsePort, Port1) {
  EXPECT_EQ(ParsePort("1"), 1);
}
TEST(ParsePort, Port65535) {
  EXPECT_EQ(ParsePort("65535"), 65535);
}
TEST(ParsePort, ZeroInvalid) {
  EXPECT_FALSE(ParsePort("0").has_value());
}
TEST(ParsePort, TooBig) {
  EXPECT_FALSE(ParsePort("65536").has_value());
}
TEST(ParsePort, Garbage) {
  EXPECT_FALSE(ParsePort("http").has_value());
}

// ── ParsePositiveInt ─────────────────────────────────────────────────────────

TEST(ParsePositiveInt, One) {
  EXPECT_EQ(ParsePositiveInt("1"), 1);
}
TEST(ParsePositiveInt, ZeroInvalid) {
  EXPECT_FALSE(ParsePositiveInt("0").has_value());
}
TEST(ParsePositiveInt, NegInvalid) {
  EXPECT_FALSE(ParsePositiveInt("-1").has_value());
}
TEST(ParsePositiveInt, Large) {
  EXPECT_EQ(ParsePositiveInt("999"), 999);
}

// ── ParseNonNegativeInt
// ───────────────────────────────────────────────────────

TEST(ParseNonNegativeInt, Zero) {
  EXPECT_EQ(ParseNonNegativeInt("0"), 0);
}
TEST(ParseNonNegativeInt, Positive) {
  EXPECT_EQ(ParseNonNegativeInt("5"), 5);
}
TEST(ParseNonNegativeInt, Neg) {
  EXPECT_FALSE(ParseNonNegativeInt("-1").has_value());
}

// ── ParseMilliseconds
// ─────────────────────────────────────────────────────────

TEST(ParseMilliseconds, Zero) {
  EXPECT_EQ(ParseMilliseconds("0"), 0);
}
TEST(ParseMilliseconds, OneSecond) {
  EXPECT_EQ(ParseMilliseconds("1000"), 1000);
}
TEST(ParseMilliseconds, TwentyFourHour) {
  EXPECT_EQ(ParseMilliseconds("86400000"), 86'400'000);
}
TEST(ParseMilliseconds, OverLimit) {
  EXPECT_FALSE(ParseMilliseconds("86400001").has_value());
}
TEST(ParseMilliseconds, Negative) {
  EXPECT_FALSE(ParseMilliseconds("-1").has_value());
}
