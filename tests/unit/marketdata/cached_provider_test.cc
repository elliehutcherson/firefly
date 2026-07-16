#include "src/marketdata/cached_provider.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/marketdata/provider.h"
#include "tests/fakes/common/fake_clock.h"
#include "tests/fakes/marketdata/fake_market_data_provider.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

constexpr CacheOptions kOptions;  // 30s quotes, 90s minute bars.

absl::Time TestNow() { return absl::UnixEpoch() + absl::Hours(1000); }

Trade TradeAt(int64_t price_e4) {
  return Trade{.price_e4 = price_e4, .time = TestNow()};
}

TEST(CachedProviderTest, SecondLookupWithinTtlHitsTheCache) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(TradeAt(1899550));
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  absl::StatusOr<Trade> first = provider.GetLatestTrade("AAPL");
  ASSERT_OK(first);
  clock.Advance(absl::Seconds(29));
  absl::StatusOr<Trade> second = provider.GetLatestTrade("AAPL");
  ASSERT_OK(second);
  EXPECT_EQ(second->price_e4, 1899550);
  EXPECT_EQ(inner.latest_trade_calls.size(), 1);
}

TEST(CachedProviderTest, ExpiredEntryRefetches) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(TradeAt(1899550));
  inner.latest_trade_results.push_back(TradeAt(1900000));
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  ASSERT_OK(provider.GetLatestTrade("AAPL"));
  clock.Advance(absl::Seconds(31));
  absl::StatusOr<Trade> second = provider.GetLatestTrade("AAPL");
  ASSERT_OK(second);
  EXPECT_EQ(second->price_e4, 1900000);
  EXPECT_EQ(inner.latest_trade_calls.size(), 2);
}

TEST(CachedProviderTest, SymbolsAreIndependentKeys) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(TradeAt(1));
  inner.latest_trade_results.push_back(TradeAt(2));
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  ASSERT_OK(provider.GetLatestTrade("AAPL"));
  ASSERT_OK(provider.GetLatestTrade("MSFT"));
  EXPECT_EQ(inner.latest_trade_calls.size(), 2);
}

TEST(CachedProviderTest, QuoteCacheEvictsEntryNearestExpirationAtLimit) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(TradeAt(1));
  inner.latest_trade_results.push_back(TradeAt(2));
  inner.latest_trade_results.push_back(TradeAt(3));
  inner.latest_trade_results.push_back(TradeAt(4));
  FakeClock clock(TestNow());
  CacheOptions options = kOptions;
  options.max_quote_entries = 2;
  CachedProvider provider(inner, clock, options);

  ASSERT_OK(provider.GetLatestTrade("AAPL"));
  clock.Advance(absl::Seconds(1));
  ASSERT_OK(provider.GetLatestTrade("MSFT"));
  clock.Advance(absl::Seconds(1));
  ASSERT_OK(provider.GetLatestTrade("GOOG"));

  // AAPL had the earliest expiration and was evicted despite still being
  // fresh, so requesting it again reaches the provider.
  absl::StatusOr<Trade> aapl = provider.GetLatestTrade("AAPL");
  ASSERT_OK(aapl);
  EXPECT_EQ(aapl->price_e4, 4);
  EXPECT_EQ(inner.latest_trade_calls.size(), 4);
}

TEST(CachedProviderTest, ErrorsAreDeliveredButNeverCached) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(absl::UnavailableError("alpaca 500"));
  inner.latest_trade_results.push_back(TradeAt(1899550));
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kUnavailable));
  // Immediately retries upstream instead of serving the cached error.
  absl::StatusOr<Trade> retry = provider.GetLatestTrade("AAPL");
  ASSERT_OK(retry);
  EXPECT_EQ(retry->price_e4, 1899550);
  EXPECT_EQ(inner.latest_trade_calls.size(), 2);
}

TEST(CachedProviderTest, DailyBarsPassThroughUncached) {
  FakeMarketDataProvider inner;
  inner.daily_bars_results.push_back(std::vector<Bar>{});
  inner.daily_bars_results.push_back(std::vector<Bar>{});
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  const absl::CivilDay start(2026, 7, 1);
  const absl::CivilDay end(2026, 7, 13);
  ASSERT_OK(provider.GetDailyBars("AAPL", start, end));
  ASSERT_OK(provider.GetDailyBars("AAPL", start, end));
  EXPECT_EQ(inner.daily_bars_calls.size(), 2);
}

TEST(CachedProviderTest, MinuteBarsCacheKeyIncludesRange) {
  FakeMarketDataProvider inner;
  inner.minute_bars_results.push_back(std::vector<Bar>{});
  inner.minute_bars_results.push_back(std::vector<Bar>{});
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  const absl::Time start = TestNow() - absl::Hours(2);
  ASSERT_OK(provider.GetMinuteBars("AAPL", start, TestNow() - absl::Minutes(16)));
  // Same range: cache hit.
  ASSERT_OK(provider.GetMinuteBars("AAPL", start, TestNow() - absl::Minutes(16)));
  EXPECT_EQ(inner.minute_bars_calls.size(), 1);
  // Different range: separate key, separate fetch.
  ASSERT_OK(provider.GetMinuteBars("AAPL", start, TestNow() - absl::Minutes(15)));
  EXPECT_EQ(inner.minute_bars_calls.size(), 2);
}

TEST(CachedProviderTest, TradeAndMinuteBarCachesAreIndependent) {
  FakeMarketDataProvider inner;
  inner.latest_trade_results.push_back(TradeAt(1));
  inner.minute_bars_results.push_back(std::vector<Bar>{});
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  ASSERT_OK(provider.GetLatestTrade("AAPL"));
  ASSERT_OK(provider.GetMinuteBars("AAPL", TestNow() - absl::Hours(1),
                                   TestNow() - absl::Minutes(16)));
  EXPECT_EQ(inner.latest_trade_calls.size(), 1);
  EXPECT_EQ(inner.minute_bars_calls.size(), 1);
}

// Blocks inside GetLatestTrade until released, to hold a fetch in flight
// while other threads look up the same key.
class BlockingProvider : public MarketDataProvider {
 public:
  absl::StatusOr<Trade> GetLatestTrade(const std::string&) override {
    calls.fetch_add(1);
    if (!entered.HasBeenNotified()) {
      entered.Notify();
    }
    release.WaitForNotification();
    return Trade{.price_e4 = 1899550, .time = absl::UnixEpoch()};
  }
  absl::StatusOr<std::vector<Bar>> GetDailyBars(const std::string&,
                                                absl::CivilDay,
                                                absl::CivilDay) override {
    return absl::UnimplementedError("unused");
  }
  absl::StatusOr<std::vector<Bar>> GetMinuteBars(const std::string&,
                                                 absl::Time,
                                                 absl::Time) override {
    return absl::UnimplementedError("unused");
  }

  std::atomic<int> calls{0};
  absl::Notification entered;
  absl::Notification release;
};

TEST(CachedProviderTest, ConcurrentMissesShareOneUpstreamCall) {
  BlockingProvider inner;
  FakeClock clock(TestNow());
  CachedProvider provider(inner, clock, kOptions);

  absl::StatusOr<Trade> results[2];
  std::thread first([&] { results[0] = provider.GetLatestTrade("AAPL"); });
  // Only join the fetch once it is definitely in flight; if the second
  // thread wrongly fetched too, calls would hit 2 and the test fails.
  inner.entered.WaitForNotification();
  std::thread second([&] { results[1] = provider.GetLatestTrade("AAPL"); });
  absl::SleepFor(absl::Milliseconds(50));  // Let it reach the shared future.
  inner.release.Notify();
  first.join();
  second.join();

  EXPECT_EQ(inner.calls.load(), 1);
  for (const absl::StatusOr<Trade>& result : results) {
    ASSERT_OK(result);
    EXPECT_EQ(result->price_e4, 1899550);
  }
}

TEST(CachedProviderTest, ZeroCapacityStillCoalescesInflightRequests) {
  BlockingProvider inner;
  FakeClock clock(TestNow());
  CacheOptions options = kOptions;
  options.max_quote_entries = 0;
  CachedProvider provider(inner, clock, options);

  absl::StatusOr<Trade> results[2];
  std::thread first([&] { results[0] = provider.GetLatestTrade("AAPL"); });
  inner.entered.WaitForNotification();
  std::thread second([&] { results[1] = provider.GetLatestTrade("AAPL"); });
  absl::SleepFor(absl::Milliseconds(50));
  inner.release.Notify();
  first.join();
  second.join();

  EXPECT_EQ(inner.calls.load(), 1);
  ASSERT_OK(results[0]);
  ASSERT_OK(results[1]);
  // Completed values are not retained at zero capacity.
  ASSERT_OK(provider.GetLatestTrade("AAPL"));
  EXPECT_EQ(inner.calls.load(), 2);
}

}  // namespace
}  // namespace firefly
