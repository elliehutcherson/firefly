#include "src/jobs/daily_bar_sync.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/marketdata/candle_repo.h"
#include "src/marketdata/instrument_repo.h"
#include "src/marketdata/provider.h"
#include "tests/fakes/common/fake_clock.h"
#include "tests/fakes/db/fake_db.h"
#include "tests/fakes/marketdata/fake_market_data_provider.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

// Noon New York on Tuesday 2026-07-14; `through` is therefore 2026-07-13.
absl::Time TestNow() {
  return absl::FromCivil(absl::CivilHour(2026, 7, 14, 12), NewYorkTimeZone());
}

Bar BarOn(absl::CivilDay day) {
  return Bar{.time = absl::FromCivil(day, NewYorkTimeZone()),
             .open_e4 = 10000,
             .high_e4 = 11000,
             .low_e4 = 9000,
             .close_e4 = 10500,
             .volume = 1000};
}

Row SymbolRow(const std::string& symbol) { return Row{{symbol}}; }

// The sync's two reads, in call order: active symbols, then latest days.
void CannedUniverse(FakeDb& db, const std::vector<std::string>& symbols,
                    const Rows& latest_days) {
  Rows symbol_rows;
  for (const std::string& s : symbols) {
    symbol_rows.push_back(SymbolRow(s));
  }
  db.query_results.push_back(symbol_rows);
  db.query_results.push_back(latest_days);
}

TEST(DailyBarSyncTest, UpToDateSymbolCostsNoProviderCalls) {
  FakeDb db;
  CannedUniverse(db, {"AAPL"}, Rows{Row{{"AAPL", "2026-07-13"}}});
  FakeMarketDataProvider provider;
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats =
      SyncDailyBars(instruments, candles, provider, clock,
                    {.backfill_start = absl::CivilDay(2026, 7, 1)});
  ASSERT_OK(stats);
  EXPECT_EQ(stats->symbols_checked, 1);
  EXPECT_EQ(stats->symbols_fetched, 0);
  EXPECT_TRUE(provider.daily_bars_calls.empty());
  EXPECT_TRUE(clock.sleeps().empty());
}

TEST(DailyBarSyncTest, StaleSymbolFetchesFromDayAfterLatest) {
  FakeDb db;
  CannedUniverse(db, {"AAPL"}, Rows{Row{{"AAPL", "2026-07-09"}}});
  db.execute_results.push_back(2);
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(std::vector<Bar>{
      BarOn(absl::CivilDay(2026, 7, 10)), BarOn(absl::CivilDay(2026, 7, 13))});
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats =
      SyncDailyBars(instruments, candles, provider, clock,
                    {.backfill_start = absl::CivilDay(2026, 7, 1)});
  ASSERT_OK(stats);
  EXPECT_EQ(stats->symbols_fetched, 1);
  EXPECT_EQ(stats->candles_written, 2);
  ASSERT_EQ(provider.daily_bars_calls.size(), 1);
  EXPECT_EQ(provider.daily_bars_calls[0].symbol, "AAPL");
  EXPECT_EQ(provider.daily_bars_calls[0].start, absl::CivilDay(2026, 7, 10));
  EXPECT_EQ(provider.daily_bars_calls[0].end, absl::CivilDay(2026, 7, 13));
  // The upsert is the third db call (symbols, latest days, insert).
  ASSERT_EQ(db.calls.size(), 3);
  EXPECT_THAT(db.calls[2].sql, HasSubstr("INSERT INTO candles_daily"));
  EXPECT_EQ(clock.sleeps().size(), 1);
}

TEST(DailyBarSyncTest, NewSymbolStartsAtBackfillStart) {
  FakeDb db;
  CannedUniverse(db, {"NEWCO"}, Rows{});
  db.execute_results.push_back(1);
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(
      std::vector<Bar>{BarOn(absl::CivilDay(2026, 7, 13))});
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats =
      SyncDailyBars(instruments, candles, provider, clock,
                    {.backfill_start = absl::CivilDay(2019, 7, 14)});
  ASSERT_OK(stats);
  ASSERT_EQ(provider.daily_bars_calls.size(), 1);
  EXPECT_EQ(provider.daily_bars_calls[0].start, absl::CivilDay(2019, 7, 14));
  EXPECT_EQ(provider.daily_bars_calls[0].end, absl::CivilDay(2026, 7, 13));
}

TEST(DailyBarSyncTest, EmptyBarsIsFineAndWritesNothing) {
  FakeDb db;
  CannedUniverse(db, {"AAPL"}, Rows{});
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(std::vector<Bar>{});
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats =
      SyncDailyBars(instruments, candles, provider, clock,
                    {.backfill_start = absl::CivilDay(2026, 7, 11)});
  ASSERT_OK(stats);
  EXPECT_EQ(stats->symbols_fetched, 1);
  EXPECT_EQ(stats->candles_written, 0);
  // No insert: only the two reads.
  EXPECT_EQ(db.calls.size(), 2);
}

TEST(DailyBarSyncTest, OneFailingSymbolDoesNotStarveTheRest) {
  FakeDb db;
  CannedUniverse(db, {"BAD", "GOOD"}, Rows{});
  db.execute_results.push_back(1);
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(absl::UnavailableError("alpaca 500"));
  provider.daily_bars_results.push_back(
      std::vector<Bar>{BarOn(absl::CivilDay(2026, 7, 13))});
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats =
      SyncDailyBars(instruments, candles, provider, clock,
                    {.backfill_start = absl::CivilDay(2026, 7, 13)});
  ASSERT_OK(stats);
  EXPECT_EQ(stats->failures, 1);
  EXPECT_EQ(stats->symbols_fetched, 1);
  EXPECT_EQ(provider.daily_bars_calls.size(), 2);
  // Throttled after failures too, so a 429 storm stays rate-bounded.
  EXPECT_EQ(clock.sleeps().size(), 2);
}

TEST(DailyBarSyncTest, AllSymbolsFailingIsAnError) {
  FakeDb db;
  CannedUniverse(db, {"AAPL"}, Rows{});
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(absl::UnavailableError("alpaca down"));
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  EXPECT_THAT(SyncDailyBars(instruments, candles, provider, clock,
                            {.backfill_start = absl::CivilDay(2026, 7, 13)}),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(DailyBarSyncTest, ExplicitSymbolsSkipTheUniverseQuery) {
  FakeDb db;
  db.query_results.push_back(Rows{});  // LatestDays only.
  db.execute_results.push_back(1);
  FakeMarketDataProvider provider;
  provider.daily_bars_results.push_back(
      std::vector<Bar>{BarOn(absl::CivilDay(2026, 7, 13))});
  FakeClock clock(TestNow());
  InstrumentRepo instruments(db);
  CandleRepo candles(db);

  absl::StatusOr<DailyBarSyncStats> stats = SyncDailyBars(
      instruments, candles, provider, clock,
      {.backfill_start = absl::CivilDay(2026, 7, 13), .symbols = {"AAPL"}});
  ASSERT_OK(stats);
  ASSERT_EQ(provider.daily_bars_calls.size(), 1);
  EXPECT_EQ(provider.daily_bars_calls[0].symbol, "AAPL");
  // No "SELECT symbol FROM instruments" call.
  EXPECT_THAT(db.calls[0].sql, HasSubstr("max(day)"));
}

}  // namespace
}  // namespace firefly
