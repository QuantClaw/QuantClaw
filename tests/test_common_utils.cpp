// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import std;

import quantclaw.common.defer;

import <gtest/gtest.h>;

// ── Defer ────────────────────────────────────────────────────────────────────

TEST(Defer, RunsOnScopeExit) {
  bool ran = false;
  {
    auto g = quantclaw::MakeDefer([&] { ran = true; });
    EXPECT_FALSE(ran);
  }
  EXPECT_TRUE(ran);
}

TEST(Defer, DismissCancels) {
  bool ran = false;
  {
    auto g = quantclaw::MakeDefer([&] { ran = true; });
    g.dismiss();
  }
  EXPECT_FALSE(ran);
}

TEST(Defer, ArmReenables) {
  bool ran = false;
  {
    auto g = quantclaw::MakeDefer([&] { ran = true; });
    g.dismiss();
    EXPECT_FALSE(g.is_active());
    g.arm();
    EXPECT_TRUE(g.is_active());
  }
  EXPECT_TRUE(ran);
}

TEST(Defer, MoveTransfersOwnership) {
  bool ran = false;
  {
    auto a = quantclaw::MakeDefer([&] { ran = true; });
    auto b = std::move(a);
    EXPECT_FALSE(a.is_active());  // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(b.is_active());
  }
  EXPECT_TRUE(ran);
}

TEST(Defer, GuardRuns) {
  bool ran = false;
  {
    auto guard = quantclaw::MakeDefer([&] { ran = true; });
    (void)guard;
  }
  EXPECT_TRUE(ran);
}

TEST(Defer, MultipleDefersRunInReverseOrder) {
  std::vector<int> order;
  {
    auto first = quantclaw::MakeDefer([&] { order.push_back(1); });
    auto second = quantclaw::MakeDefer([&] { order.push_back(2); });
    auto third = quantclaw::MakeDefer([&] { order.push_back(3); });
    (void)first;
    (void)second;
    (void)third;
  }
  // C++ local destructors unwind in reverse declaration order
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 3);
  EXPECT_EQ(order[1], 2);
  EXPECT_EQ(order[2], 1);
}
