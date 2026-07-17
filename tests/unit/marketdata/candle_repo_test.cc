#include "src/marketdata/candle_repo.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/fakes/db/fake_db.h"
#include "src/common/symbol.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

// Parse-or-die for test literals; validity is Symbol's own tested contract.
Symbol Sym(absl::string_view raw) { return *Symbol::Parse(raw); }

Row CandleRow(const std::string& day, const std::string& open,
              const std::string& high, const std::string& low,
              const std::string& close, const std::string& volume) {
  return Row{{day, open, high, low, close, volume}};
}

TEST(CandleRepoTest, GetRangeParsesRowsAndBindsParams) {
  FakeDb db;
  db.query_results.push_back(Rows{
      CandleRow("2026-07-10", "189.9550", "191.0000", "188.5000", "190.1200",
                "48210000"),
      CandleRow("2026-07-13", "190.5000", "192.0000", "190.0000", "191.7500",
                "51000000")});
  CandleRepo repo(db);

  absl::StatusOr<std::vector<DailyCandle>> candles = repo.GetRange(Sym("AAPL"), absl::CivilDay(2026, 7, 1), absl::CivilDay(2026, 7, 14));
  ASSERT_OK(candles);
  ASSERT_EQ(candles->size(), 2);
  EXPECT_EQ((*candles)[0].day, absl::CivilDay(2026, 7, 10));
  EXPECT_EQ((*candles)[0].open_e4, 1899550);
  EXPECT_EQ((*candles)[0].high_e4, 1910000);
  EXPECT_EQ((*candles)[0].low_e4, 1885000);
  EXPECT_EQ((*candles)[0].close_e4, 1901200);
  EXPECT_EQ((*candles)[0].volume, 48210000);
  EXPECT_EQ((*candles)[1].day, absl::CivilDay(2026, 7, 13));

  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].params,
              ElementsAre("AAPL", "2026-07-01", "2026-07-14"));
}

TEST(CandleRepoTest, GetRangeRejectsMalformedNumeric) {
  FakeDb db;
  db.query_results.push_back(Rows{CandleRow(
      "2026-07-10", "not-a-price", "1", "1", "1", "1")});
  CandleRepo repo(db);

  EXPECT_THAT(repo.GetRange(Sym("AAPL"), absl::CivilDay(2026, 7, 1),
                            absl::CivilDay(2026, 7, 14)),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CandleRepoTest, GetRangeRejectsNullColumn) {
  FakeDb db;
  Row row = CandleRow("2026-07-10", "1", "1", "1", "1", "1");
  row.columns[4] = std::nullopt;
  db.query_results.push_back(Rows{row});
  CandleRepo repo(db);

  EXPECT_THAT(repo.GetRange(Sym("AAPL"), absl::CivilDay(2026, 7, 1),
                            absl::CivilDay(2026, 7, 14)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(CandleRepoTest, UpsertCandlesBuildsArraysInOneStatement) {
  FakeDb db;
  db.execute_results.push_back(2);
  CandleRepo repo(db);

  EXPECT_OK(repo.UpsertCandles(Sym("AAPL"), {{.day = absl::CivilDay(2026, 7, 10),
                .open_e4 = 1899550,
                .high_e4 = 1910000,
                .low_e4 = 1885000,
                .close_e4 = 1901200,
                .volume = 48210000},
               {.day = absl::CivilDay(2026, 7, 13),
                .open_e4 = 1905000,
                .high_e4 = 1920000,
                .low_e4 = 1900000,
                .close_e4 = 1917500,
                .volume = 51000000}}));

  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("unnest"));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("ON CONFLICT (symbol, day)"));
  EXPECT_THAT(db.calls[0].params,
              ElementsAre("AAPL", "{2026-07-10,2026-07-13}",
                          "{189.9550,190.5000}", "{191.0000,192.0000}",
                          "{188.5000,190.0000}", "{190.1200,191.7500}",
                          "{48210000,51000000}"));
}

TEST(CandleRepoTest, UpsertNothingIsANoOp) {
  FakeDb db;
  CandleRepo repo(db);

  EXPECT_OK(repo.UpsertCandles(Sym("AAPL"), {}));
  EXPECT_TRUE(db.calls.empty());
}

TEST(CandleRepoTest, LatestDaysMapsRows) {
  FakeDb db;
  db.query_results.push_back(
      Rows{Row{{"AAPL", "2026-07-13"}}, Row{{"MSFT", "2026-07-10"}}});
  CandleRepo repo(db);

  absl::StatusOr<absl::flat_hash_map<Symbol, absl::CivilDay>> latest =
      repo.LatestDays();
  ASSERT_OK(latest);
  ASSERT_EQ(latest->size(), 2);
  EXPECT_EQ(latest->at(Sym("AAPL")), absl::CivilDay(2026, 7, 13));
  EXPECT_EQ(latest->at(Sym("MSFT")), absl::CivilDay(2026, 7, 10));
}

}  // namespace
}  // namespace firefly
