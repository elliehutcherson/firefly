#include "src/api/market_handlers.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "src/common/clock.h"
#include "src/marketdata/candle_repo.h"
#include "src/marketdata/instrument_repo.h"
#include "src/marketdata/provider.h"
#include "tests/fakes/fake_clock.h"
#include "tests/fakes/fake_db.h"
#include "tests/fakes/fake_market_data_provider.h"
#include "tests/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;

// Tuesday 2026-07-14, 15:00 New York (a regular trading afternoon).
absl::Time TestNow() {
  return absl::FromCivil(absl::CivilHour(2026, 7, 14, 15), NewYorkTimeZone());
}

// Owns the fakes and hands out a MarketDeps wired to them.
class MarketHandlersTest : public ::testing::Test {
 protected:
  MarketDeps Deps() {
    return {.instruments = &instruments_,
            .candles = &candles_,
            .provider = &provider_,
            .clock = &clock_};
  }

  void SymbolExists() { db_.query_results.push_back(Rows{Row{{"1"}}}); }
  void SymbolMissing() { db_.query_results.push_back(Rows{}); }

  FakeDb db_;
  InstrumentRepo instruments_{&db_};
  CandleRepo candles_{&db_};
  FakeMarketDataProvider provider_;
  FakeClock clock_{TestNow()};
};

TEST(NormalizeSymbolTest, AcceptsAndUppercasesValidSymbols) {
  EXPECT_THAT(NormalizeSymbol("aapl"), IsOkAndHolds("AAPL"));
  EXPECT_THAT(NormalizeSymbol("AAPL"), IsOkAndHolds("AAPL"));
  EXPECT_THAT(NormalizeSymbol("brk.b"), IsOkAndHolds("BRK.B"));
  EXPECT_THAT(NormalizeSymbol("A"), IsOkAndHolds("A"));
}

TEST(NormalizeSymbolTest, RejectsInjectionAndJunk) {
  for (const char* junk :
       {"", ".", "-", ".AAPL", "-AAPL", "AAPL; DROP TABLE users",
        "TOOLONGSYMBOL", "AA PL", "AAPL'", "aapl%27", "A/B", "1AAPL"}) {
    EXPECT_THAT(NormalizeSymbol(junk),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "input: '" << junk << "'";
  }
}

TEST_F(MarketHandlersTest, UnknownSymbolIs404AndNeverReachesProvider) {
  SymbolMissing();
  EXPECT_THAT(GetQuoteJson(Deps(), "ZZZZ"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_TRUE(provider_.latest_trade_calls.empty());
}

TEST_F(MarketHandlersTest, QuoteHappyPath) {
  SymbolExists();
  provider_.latest_trade_results.push_back(
      Trade{.price_e4 = 1899550, .time = TestNow()});

  absl::StatusOr<nlohmann::json> quote = GetQuoteJson(Deps(), "aapl");
  ASSERT_OK(quote);
  EXPECT_EQ((*quote)["symbol"], "AAPL");
  EXPECT_EQ((*quote)["price"], "189.9550");
  EXPECT_EQ((*quote)["time"], "2026-07-14T19:00:00Z");
}

TEST_F(MarketHandlersTest, NullProviderMeansQuoteAndIntradayUnavailable) {
  MarketDeps deps = Deps();
  deps.provider = nullptr;
  SymbolExists();
  EXPECT_THAT(GetQuoteJson(deps, "AAPL"),
              StatusIs(absl::StatusCode::kUnavailable));
  SymbolExists();
  EXPECT_THAT(GetIntradayCandlesJson(deps, "AAPL"),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(MarketHandlersTest, DailyServesFromPostgresWithoutAProvider) {
  MarketDeps deps = Deps();
  deps.provider = nullptr;
  SymbolExists();
  db_.query_results.push_back(Rows{Row{
      {"2026-07-13", "190.5000", "192.0000", "190.0000", "191.7500",
       "51000000"}}});

  absl::StatusOr<nlohmann::json> daily =
      GetDailyCandlesJson(deps, "AAPL", "month");
  ASSERT_OK(daily);
  EXPECT_EQ((*daily)["symbol"], "AAPL");
  EXPECT_EQ((*daily)["range"], "month");
  ASSERT_EQ((*daily)["bars"].size(), 1);
  EXPECT_EQ((*daily)["bars"][0]["day"], "2026-07-13");
  EXPECT_EQ((*daily)["bars"][0]["close"], "191.7500");
  EXPECT_EQ((*daily)["bars"][0]["volume"], 51000000);
}

TEST_F(MarketHandlersTest, DailyRangesComputeNewYorkWindows) {
  const struct {
    const char* range;
    const char* start;
  } cases[] = {{"month", "2026-06-14"},
               {"ytd", "2026-01-01"},
               {"year", "2025-07-14"}};
  for (const auto& c : cases) {
    SymbolExists();
    db_.query_results.push_back(Rows{});
    ASSERT_OK(GetDailyCandlesJson(Deps(), "AAPL", c.range));
    EXPECT_THAT(db_.calls.back().params,
                ElementsAre("AAPL", c.start, "2026-07-14"))
        << "range: " << c.range;
  }
}

TEST_F(MarketHandlersTest, BadRangeIsInvalidArgument) {
  SymbolExists();
  EXPECT_THAT(GetDailyCandlesJson(Deps(), "AAPL", "fortnight"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(MarketHandlersTest, IntradayRequestsTodaysSessionQuantized) {
  SymbolExists();
  provider_.minute_bars_results.push_back(std::vector<Bar>{});

  ASSERT_OK(GetIntradayCandlesJson(Deps(), "AAPL"));
  ASSERT_EQ(provider_.minute_bars_calls.size(), 1);
  const FakeMinuteBarsCall& call = provider_.minute_bars_calls[0];
  const absl::TimeZone ny = NewYorkTimeZone();
  // 09:30 NY through now (15:00) minus the 16-minute delay margin, floored
  // to the whole minute: 14:44:00.
  EXPECT_EQ(call.start,
            absl::FromCivil(absl::CivilMinute(2026, 7, 14, 9, 30), ny));
  EXPECT_EQ(call.end,
            absl::FromCivil(absl::CivilMinute(2026, 7, 14, 14, 44), ny));
}

TEST_F(MarketHandlersTest, IntradayBeforeTheOpenFallsBackToLastSession) {
  clock_.SetNow(
      absl::FromCivil(absl::CivilHour(2026, 7, 14, 8), NewYorkTimeZone()));
  SymbolExists();
  provider_.minute_bars_results.push_back(std::vector<Bar>{});

  ASSERT_OK(GetIntradayCandlesJson(Deps(), "AAPL"));
  ASSERT_EQ(provider_.minute_bars_calls.size(), 1);
  const FakeMinuteBarsCall& call = provider_.minute_bars_calls[0];
  const absl::TimeZone ny = NewYorkTimeZone();
  // The previous day's full session, clamped to the 16:00 close.
  EXPECT_EQ(call.start,
            absl::FromCivil(absl::CivilMinute(2026, 7, 13, 9, 30), ny));
  EXPECT_EQ(call.end,
            absl::FromCivil(absl::CivilMinute(2026, 7, 13, 16, 0), ny));
}

TEST_F(MarketHandlersTest, IntradayAfterHoursClampsToTheClose) {
  clock_.SetNow(
      absl::FromCivil(absl::CivilHour(2026, 7, 14, 20), NewYorkTimeZone()));
  SymbolExists();
  provider_.minute_bars_results.push_back(
      std::vector<Bar>{Bar{.time = TestNow(),
                           .open_e4 = 10000,
                           .high_e4 = 11000,
                           .low_e4 = 9000,
                           .close_e4 = 10500,
                           .volume = 1000}});

  absl::StatusOr<nlohmann::json> intraday =
      GetIntradayCandlesJson(Deps(), "AAPL");
  ASSERT_OK(intraday);
  const FakeMinuteBarsCall& call = provider_.minute_bars_calls[0];
  EXPECT_EQ(call.end, absl::FromCivil(absl::CivilMinute(2026, 7, 14, 16, 0),
                                      NewYorkTimeZone()));
  ASSERT_EQ((*intraday)["bars"].size(), 1);
  EXPECT_EQ((*intraday)["bars"][0]["close"], "1.0500");
}

TEST(HttpStatusFromCodeTest, MapsCodes) {
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kInvalidArgument), 400);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnauthenticated), 401);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kPermissionDenied), 403);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kNotFound), 404);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kAlreadyExists), 409);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kResourceExhausted), 429);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnavailable), 503);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kDeadlineExceeded), 504);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kInternal), 500);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnknown), 500);
}

}  // namespace
}  // namespace firefly
