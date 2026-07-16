#include "src/marketdata/instrument_repo.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/fakes/db/fake_db.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

Row SymbolRow(const std::string& symbol) { return Row{{symbol}}; }

TEST(InstrumentRepoTest, ListActiveSymbols) {
  FakeDb db;
  db.query_results.push_back(
      Rows{SymbolRow("AAPL"), SymbolRow("MSFT"), SymbolRow("SPY")});
  InstrumentRepo repo(db);

  absl::StatusOr<std::vector<std::string>> symbols = repo.ListActiveSymbols();
  ASSERT_OK(symbols);
  EXPECT_THAT(*symbols, ElementsAre("AAPL", "MSFT", "SPY"));
  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("is_active"));
}

TEST(InstrumentRepoTest, ListPropagatesDbError) {
  FakeDb db;
  db.query_results.push_back(absl::UnavailableError("db down"));
  InstrumentRepo repo(db);

  EXPECT_THAT(repo.ListActiveSymbols(),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(InstrumentRepoTest, NullSymbolIsInternalError) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{std::nullopt}}});
  InstrumentRepo repo(db);

  EXPECT_THAT(repo.ListActiveSymbols(),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(InstrumentRepoTest, ExistsBindsSymbolAndMapsRows) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1"}}});
  db.query_results.push_back(Rows{});
  InstrumentRepo repo(db);

  absl::StatusOr<bool> exists = repo.Exists("AAPL");
  ASSERT_OK(exists);
  EXPECT_TRUE(*exists);

  absl::StatusOr<bool> missing = repo.Exists("ZZZZ");
  ASSERT_OK(missing);
  EXPECT_FALSE(*missing);

  ASSERT_EQ(db.calls.size(), 2);
  EXPECT_THAT(db.calls[0].params, ElementsAre("AAPL"));
}

}  // namespace
}  // namespace firefly
