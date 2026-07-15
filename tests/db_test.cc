#include "src/common/db.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/time/civil_time.h"
#include "gtest/gtest.h"
#include "src/common/config.h"
#include "src/common/money.h"
#include "src/marketdata/candle_repo.h"
#include "tests/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

// Integration tests against the docker-compose Postgres. Skipped when no
// database is reachable (start one with: docker compose up -d db).
class DbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<Db>> db =
        OpenDb(Config::FromEnv().database_url, /*pool_size=*/2);
    if (!db.ok()) {
      GTEST_SKIP() << "database unavailable: " << db.status();
    }
    db_ = std::move(*db);
  }

  std::unique_ptr<Db> db_;
};

TEST_F(DbTest, Ping) { EXPECT_OK(db_->Ping()); }

TEST_F(DbTest, QueryReturnsRows) {
  absl::StatusOr<Rows> rows = db_->Query("SELECT 1 + 1, 'hello'");
  ASSERT_OK(rows);
  ASSERT_EQ(rows->size(), 1);
  ASSERT_EQ((*rows)[0].columns.size(), 2);
  EXPECT_EQ((*rows)[0].columns[0], "2");
  EXPECT_EQ((*rows)[0].columns[1], "hello");
}

TEST_F(DbTest, BindsParametersAndNull) {
  absl::StatusOr<Rows> rows =
      db_->Query("SELECT $1::text, $2::text", {"world", std::nullopt});
  ASSERT_OK(rows);
  ASSERT_EQ(rows->size(), 1);
  EXPECT_EQ((*rows)[0].columns[0], "world");
  EXPECT_EQ((*rows)[0].columns[1], std::nullopt);
}

TEST_F(DbTest, InvalidSqlIsInvalidArgument) {
  EXPECT_THAT(db_->Query("SELECT FROM WHERE"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(DbTest, NumericRoundTripsPriceE4) {
  // Pins the money.h contract: e4 prices survive NUMERIC(14,4) exactly.
  absl::StatusOr<Rows> rows =
      db_->Query("SELECT $1::numeric(14,4)", {PriceE4ToString(1899550)});
  ASSERT_OK(rows);
  EXPECT_EQ((*rows)[0].columns[0], "189.9550");
}

TEST_F(DbTest, MigrationsApplied) {
  // schema_migrations exists once scripts/migrate.sh has run.
  absl::StatusOr<Rows> rows =
      db_->Query("SELECT count(*) FROM schema_migrations");
  ASSERT_OK(rows);
  EXPECT_NE((*rows)[0].columns[0], "0");
}

TEST_F(DbTest, InstrumentUniverseSeeded) {
  // 0002_seed_instruments.sql: ~S&P 500 + Nasdaq-100 + popular ETFs.
  absl::StatusOr<Rows> rows =
      db_->Query("SELECT count(*) FROM instruments WHERE is_active");
  ASSERT_OK(rows);
  ASSERT_TRUE((*rows)[0].columns[0].has_value());
  EXPECT_GE(std::stoi(*(*rows)[0].columns[0]), 500);
}

TEST_F(DbTest, CandleRepoRoundTripsThroughRealPostgres) {
  // Exercises the unnest batch insert, the ON CONFLICT idempotence, and the
  // NUMERIC(14,4) -> e4 read path against a real database. Uses 1970 dates
  // so it can never collide with backfilled market data; AAPL exists via the
  // seed migration (candles_daily has a foreign key on instruments).
  CandleRepo repo(db_.get());
  const absl::CivilDay kDay1(1970, 1, 2);
  const absl::CivilDay kDay2(1970, 1, 5);
  ASSERT_OK(db_->Execute(
      "DELETE FROM candles_daily WHERE symbol = 'AAPL' AND day < '1971-01-01'"));

  const std::vector<DailyCandle> candles = {{.day = kDay1,
                                             .open_e4 = 1899550,
                                             .high_e4 = 1910000,
                                             .low_e4 = 1885000,
                                             .close_e4 = 1901200,
                                             .volume = 48210000},
                                            {.day = kDay2,
                                             .open_e4 = 1905000,
                                             .high_e4 = 1920000,
                                             .low_e4 = 1900000,
                                             .close_e4 = 1917500,
                                             .volume = 51000000}};
  ASSERT_OK(repo.UpsertCandles("AAPL", candles));
  // Idempotent: re-inserting the same days is a no-op, not an error.
  ASSERT_OK(repo.UpsertCandles("AAPL", candles));

  absl::StatusOr<std::vector<DailyCandle>> stored =
      repo.GetRange("AAPL", kDay1, kDay2);
  ASSERT_OK(stored);
  ASSERT_EQ(stored->size(), 2);
  EXPECT_EQ((*stored)[0].day, kDay1);
  EXPECT_EQ((*stored)[0].open_e4, 1899550);
  EXPECT_EQ((*stored)[0].close_e4, 1901200);
  EXPECT_EQ((*stored)[0].volume, 48210000);
  EXPECT_EQ((*stored)[1].day, kDay2);
  EXPECT_EQ((*stored)[1].high_e4, 1920000);

  absl::StatusOr<absl::flat_hash_map<std::string, absl::CivilDay>> latest =
      repo.LatestDays();
  ASSERT_OK(latest);
  ASSERT_TRUE(latest->contains("AAPL"));
  EXPECT_GE(latest->at("AAPL"), kDay2);

  ASSERT_OK(db_->Execute(
      "DELETE FROM candles_daily WHERE symbol = 'AAPL' AND day < '1971-01-01'"));
}

TEST(DbOpenTest, BadUrlIsUnavailable) {
  EXPECT_THAT(
      OpenDb("postgres://nobody:wrong@localhost:1/none", /*pool_size=*/1),
      StatusIs(absl::StatusCode::kUnavailable));
}

TEST(DbOpenTest, RejectsNonPositivePoolSize) {
  EXPECT_THAT(OpenDb("postgres://localhost/x", /*pool_size=*/0),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace firefly
