#include "src/trading/trading_repo.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/common/symbol.h"
#include "src/trading/order_math.h"
#include "tests/fakes/db/fake_db.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;

Symbol Aapl() { return Symbol::Parse("AAPL").value(); }

TEST(TradingRepoTest, HappyBuyIssuesTheExactStatementSequence) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1000000"}}});  // lock cash
  db.query_results.push_back(Rows{});                  // no position
  db.query_results.push_back(Rows{});                  // no other shorts
  db.query_results.push_back(Rows{Row{{"77"}}});       // order id
  db.execute_results.push_back(1);                     // cash update
  db.execute_results.push_back(1);                     // position insert
  TradingRepo repo(db);

  absl::StatusOr<OrderResult> result =
      repo.ExecuteOrder(42, Aapl(), OrderSide::kBuy, 2, 1899550);
  ASSERT_OK(result);
  EXPECT_EQ(result->order_id, 77);
  EXPECT_EQ(result->price_e4, 1899550);
  EXPECT_EQ(result->cash_delta_cents, -37991);
  EXPECT_EQ(result->cash_cents, 962009);
  EXPECT_EQ(result->position_quantity, 2);
  EXPECT_EQ(result->position_avg_price_e4, 1899550);

  ASSERT_EQ(db.calls.size(), 6);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("FOR UPDATE"));
  EXPECT_THAT(db.calls[0].params, ElementsAre("42"));
  EXPECT_THAT(db.calls[1].sql,
              HasSubstr("WHERE user_id = $1 AND symbol = $2"));
  EXPECT_THAT(db.calls[1].params, ElementsAre("42", "AAPL"));
  EXPECT_THAT(db.calls[2].sql, HasSubstr("quantity < 0 AND symbol <> $2"));
  EXPECT_THAT(db.calls[3].sql, HasSubstr("INSERT INTO orders"));
  EXPECT_THAT(db.calls[3].params,
              ElementsAre("42", "AAPL", "buy", "2", "189.9550", "-37991"));
  EXPECT_THAT(db.calls[4].sql, HasSubstr("UPDATE users SET cash_cents"));
  EXPECT_THAT(db.calls[4].params, ElementsAre("42", "962009"));
  EXPECT_THAT(db.calls[5].sql, HasSubstr("INSERT INTO positions"));
  EXPECT_THAT(db.calls[5].params,
              ElementsAre("42", "AAPL", "2", "189.9550"));
  EXPECT_EQ(db.transaction_begins, 1);
  EXPECT_EQ(db.transaction_commits, 1);
}

TEST(TradingRepoTest, SellToZeroDeletesThePositionAndSkipsExposure) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1000"}}});
  db.query_results.push_back(Rows{Row{{"2", "100.0000"}}});
  db.query_results.push_back(Rows{Row{{"5"}}});  // order id
  db.execute_results.push_back(1);               // cash update
  db.execute_results.push_back(1);               // position delete
  TradingRepo repo(db);

  absl::StatusOr<OrderResult> result =
      repo.ExecuteOrder(42, Aapl(), OrderSide::kSell, 2, 1000000);
  ASSERT_OK(result);
  EXPECT_EQ(result->cash_delta_cents, 20000);
  EXPECT_EQ(result->position_quantity, 0);

  ASSERT_EQ(db.calls.size(), 5);
  for (const FakeDbCall& call : db.calls) {
    EXPECT_THAT(call.sql, Not(HasSubstr("quantity < 0")));
  }
  EXPECT_THAT(db.calls[4].sql, HasSubstr("DELETE FROM positions"));
  EXPECT_THAT(db.calls[4].params, ElementsAre("42", "AAPL"));
  EXPECT_EQ(db.transaction_commits, 1);
}

TEST(TradingRepoTest, PartialCoverUpdatesThePositionAndSkipsExposure) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"100000"}}});
  db.query_results.push_back(Rows{Row{{"-2", "100.0000"}}});
  db.query_results.push_back(Rows{Row{{"9"}}});
  db.execute_results.push_back(1);
  db.execute_results.push_back(1);
  TradingRepo repo(db);

  absl::StatusOr<OrderResult> result =
      repo.ExecuteOrder(42, Aapl(), OrderSide::kCover, 1, 1000001);
  ASSERT_OK(result);
  EXPECT_EQ(result->cash_delta_cents, -10001);
  EXPECT_EQ(result->position_quantity, -1);

  ASSERT_EQ(db.calls.size(), 5);
  for (const FakeDbCall& call : db.calls) {
    EXPECT_THAT(call.sql, Not(HasSubstr("quantity < 0")));
  }
  EXPECT_THAT(db.calls[4].sql, HasSubstr("UPDATE positions"));
  EXPECT_THAT(db.calls[4].sql, HasSubstr("updated_at = now()"));
  EXPECT_THAT(db.calls[4].params,
              ElementsAre("42", "AAPL", "-1", "100.0000"));
}

TEST(TradingRepoTest, RejectionStopsBeforeAnyWriteWithZeroCommits) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1000"}}});
  db.query_results.push_back(Rows{});  // flat, so sell is wrong-direction
  TradingRepo repo(db);

  EXPECT_THAT(repo.ExecuteOrder(42, Aapl(), OrderSide::kSell, 1, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("no long position")));
  ASSERT_EQ(db.calls.size(), 2);
  EXPECT_EQ(db.transaction_begins, 1);
  EXPECT_EQ(db.transaction_commits, 0);
}

TEST(TradingRepoTest, MarginRejectionSeesOtherShortExposure) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"100000"}}});
  db.query_results.push_back(Rows{});
  // One other short: 1 @ $500 -> 50,000c exposure, so a $400 buy leaves
  // 4 * 60,000 < 5 * 50,000.
  db.query_results.push_back(Rows{Row{{"-1", "500.0000"}}});
  TradingRepo repo(db);

  EXPECT_THAT(repo.ExecuteOrder(42, Aapl(), OrderSide::kBuy, 1, 4000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("margin")));
  ASSERT_EQ(db.calls.size(), 3);
  EXPECT_EQ(db.transaction_commits, 0);
}

TEST(TradingRepoTest, MissingUsersRowIsInternal) {
  FakeDb db;
  db.query_results.push_back(Rows{});
  TradingRepo repo(db);

  EXPECT_THAT(repo.ExecuteOrder(42, Aapl(), OrderSide::kBuy, 1, 1000000),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(db.transaction_commits, 0);
}

TEST(TradingRepoTest, JunkPositionRowIsInternal) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1000"}}});
  db.query_results.push_back(Rows{Row{{"2", "banana"}}});
  TradingRepo repo(db);

  EXPECT_THAT(repo.ExecuteOrder(42, Aapl(), OrderSide::kSell, 1, 1000000),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(db.transaction_commits, 0);
}

TEST(TradingRepoTest, WrongAffectedCountOnCashUpdateIsInternal) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"1000000"}}});
  db.query_results.push_back(Rows{});
  db.query_results.push_back(Rows{});
  db.query_results.push_back(Rows{Row{{"77"}}});
  db.execute_results.push_back(0);  // cash update misses
  TradingRepo repo(db);

  EXPECT_THAT(repo.ExecuteOrder(42, Aapl(), OrderSide::kBuy, 1, 1000000),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(db.transaction_commits, 0);
}

TEST(TradingRepoTest, GetPortfolioMapsCashAndPositions) {
  FakeDb db;
  db.query_results.push_back(Rows{
      Row{{"962009", "AAPL", "2", "189.9550"}},
      Row{{"962009", "TSLA", "-1", "250.0000"}},
  });
  TradingRepo repo(db);

  absl::StatusOr<Portfolio> portfolio = repo.GetPortfolio(42);
  ASSERT_OK(portfolio);
  EXPECT_EQ(portfolio->cash_cents, 962009);
  ASSERT_EQ(portfolio->positions.size(), 2);
  EXPECT_EQ(portfolio->positions[0].symbol.str(), "AAPL");
  EXPECT_EQ(portfolio->positions[0].quantity, 2);
  EXPECT_EQ(portfolio->positions[0].avg_price_e4, 1899550);
  EXPECT_EQ(portfolio->positions[1].symbol.str(), "TSLA");
  EXPECT_EQ(portfolio->positions[1].quantity, -1);
  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("LEFT JOIN positions"));
  EXPECT_THAT(db.calls[0].params, ElementsAre("42"));
}

TEST(TradingRepoTest, GetPortfolioFlatAccountHasNoPositions) {
  FakeDb db;
  db.query_results.push_back(
      Rows{Row{{"1000000", std::nullopt, std::nullopt, std::nullopt}}});
  TradingRepo repo(db);

  absl::StatusOr<Portfolio> portfolio = repo.GetPortfolio(42);
  ASSERT_OK(portfolio);
  EXPECT_EQ(portfolio->cash_cents, 1000000);
  EXPECT_TRUE(portfolio->positions.empty());
}

TEST(TradingRepoTest, GetPortfolioUnknownUserIsNotFound) {
  FakeDb db;
  db.query_results.push_back(Rows{});
  TradingRepo repo(db);

  EXPECT_THAT(repo.GetPortfolio(42), StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace firefly
