#include "src/api/rate_limiter.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "tests/fakes/fake_clock.h"

namespace firefly {
namespace {

absl::Time TestNow() { return absl::FromUnixSeconds(1786000000); }

TEST(RateLimiterTest, AllowsBurstThenDenies) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 3});

  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_FALSE(limiter.Allow("ip:1"));
}

TEST(RateLimiterTest, RefillsOverTime) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 2});

  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_FALSE(limiter.Allow("ip:1"));
  clock.Advance(absl::Seconds(1));
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_FALSE(limiter.Allow("ip:1"));
}

TEST(RateLimiterTest, RefillCapsAtBurst) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 2});

  EXPECT_TRUE(limiter.Allow("ip:1"));
  clock.Advance(absl::Hours(10));  // Refill far beyond burst.
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_FALSE(limiter.Allow("ip:1"));
}

TEST(RateLimiterTest, KeysAreIndependent) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 1});

  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_FALSE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:2"));
}

TEST(RateLimiterTest, SweepsIdleKeysAtCapacity) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 1,
                                          .max_keys = 2});

  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:2"));
  clock.Advance(absl::Seconds(10));  // Both buckets refill to full = idle.
  // Map is at max_keys; the sweep clears the idle buckets and admits ip:3.
  EXPECT_TRUE(limiter.Allow("ip:3"));
  EXPECT_FALSE(limiter.Allow("ip:3"));  // Its own bucket still enforces.
}

TEST(RateLimiterTest, FailsOpenWhenAllKeysActive) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 0.001,
                                          .burst = 1,
                                          .max_keys = 2});

  // Drain two buckets; neither can refill within the sweep horizon.
  EXPECT_TRUE(limiter.Allow("ip:1"));
  EXPECT_TRUE(limiter.Allow("ip:2"));
  // A third key can't evict anything: availability wins, request allowed.
  EXPECT_TRUE(limiter.Allow("ip:3"));
}

TEST(RateLimiterTest, ThreadedSmoke) {
  FakeClock clock(TestNow());
  TokenBucketRateLimiter limiter(&clock, {.tokens_per_second = 1.0,
                                          .burst = 100});

  std::atomic<int> allowed{0};
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 50; ++i) {
        if (limiter.Allow("shared")) {
          allowed.fetch_add(1);
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  // 200 attempts against a burst of 100 with no time passing.
  EXPECT_EQ(allowed.load(), 100);
}

}  // namespace
}  // namespace firefly
