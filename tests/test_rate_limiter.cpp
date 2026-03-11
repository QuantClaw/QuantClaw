// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include <thread>

#include "quantclaw/security/rate_limiter.hpp"

#include <gtest/gtest.h>

using namespace quantclaw;

TEST(RateLimiter, AllowsUnderLimit) {
  RateLimiter::Config cfg;
  cfg.max_requests = 5;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;  // disable burst check for this test

  RateLimiter limiter(cfg);

  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.Allow("client1"));
  }
  // 6th request should be denied
  EXPECT_FALSE(limiter.Allow("client1"));
}

TEST(RateLimiter, SeparateClients) {
  RateLimiter::Config cfg;
  cfg.max_requests = 2;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;

  RateLimiter limiter(cfg);

  EXPECT_TRUE(limiter.Allow("client1"));
  EXPECT_TRUE(limiter.Allow("client1"));
  EXPECT_FALSE(limiter.Allow("client1"));

  // client2 is independent
  EXPECT_TRUE(limiter.Allow("client2"));
  EXPECT_TRUE(limiter.Allow("client2"));
  EXPECT_FALSE(limiter.Allow("client2"));
}

TEST(RateLimiter, Remaining) {
  RateLimiter::Config cfg;
  cfg.max_requests = 3;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;

  RateLimiter limiter(cfg);

  EXPECT_EQ(limiter.Remaining("client1"), 3);
  limiter.Allow("client1");
  EXPECT_EQ(limiter.Remaining("client1"), 2);
  limiter.Allow("client1");
  EXPECT_EQ(limiter.Remaining("client1"), 1);
  limiter.Allow("client1");
  EXPECT_EQ(limiter.Remaining("client1"), 0);
}

TEST(RateLimiter, Reset) {
  RateLimiter::Config cfg;
  cfg.max_requests = 2;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;

  RateLimiter limiter(cfg);

  limiter.Allow("client1");
  limiter.Allow("client1");
  EXPECT_FALSE(limiter.Allow("client1"));

  limiter.Reset("client1");
  EXPECT_TRUE(limiter.Allow("client1"));
}

TEST(RateLimiter, ResetAll) {
  RateLimiter::Config cfg;
  cfg.max_requests = 1;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;

  RateLimiter limiter(cfg);

  limiter.Allow("client1");
  limiter.Allow("client2");
  EXPECT_FALSE(limiter.Allow("client1"));
  EXPECT_FALSE(limiter.Allow("client2"));

  limiter.Reset();
  EXPECT_TRUE(limiter.Allow("client1"));
  EXPECT_TRUE(limiter.Allow("client2"));
}

TEST(RateLimiter, Configure) {
  RateLimiter limiter;

  RateLimiter::Config cfg;
  cfg.max_requests = 2;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;
  limiter.Configure(cfg);

  EXPECT_TRUE(limiter.Allow("x"));
  EXPECT_TRUE(limiter.Allow("x"));
  EXPECT_FALSE(limiter.Allow("x"));
}

TEST(RateLimiter, RetryAfter) {
  RateLimiter::Config cfg;
  cfg.max_requests = 1;
  cfg.window_seconds = 60;
  cfg.burst_max = 0;

  RateLimiter limiter(cfg);

  EXPECT_EQ(limiter.RetryAfter("client1"), 0);
  limiter.Allow("client1");
  // After hitting limit, retry_after should be > 0
  int retry = limiter.RetryAfter("client1");
  EXPECT_GT(retry, 0);
  EXPECT_LE(retry, 61);  // Should be within window
}

TEST(RateLimiter, DefaultConfigAllowsManyRequests) {
  RateLimiter limiter;

  // Default config allows 100 requests
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(limiter.Allow("client1"));
  }
}

TEST(RateLimiter, BurstLimit) {
  RateLimiter::Config cfg;
  cfg.max_requests = 100;
  cfg.window_seconds = 60;
  cfg.burst_max = 3;

  RateLimiter limiter(cfg);

  // Should allow 3 burst requests
  EXPECT_TRUE(limiter.Allow("client1"));
  EXPECT_TRUE(limiter.Allow("client1"));
  EXPECT_TRUE(limiter.Allow("client1"));
  // 4th in same second should be rejected
  EXPECT_FALSE(limiter.Allow("client1"));
}
