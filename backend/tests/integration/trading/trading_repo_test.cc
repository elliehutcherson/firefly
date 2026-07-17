#include "trading/trading_repo.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "common/config.h"
#include "common/symbol.h"
#include "db/db.h"
#include "trading/order_math.h"
#include "support/status_matchers.h"
#include "support/symbol.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

// Real-Postgres proof of the transaction behavior scripted fakes cannot
// show: rollback leaves no trace, and the users-row FOR UPDATE lock forces
// concurrent orders to serialize and revalidate against committed state.
// Skipped when no database is reachable (docker compose up -d db).
class TradingRepoIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<Db>> db =
        OpenDb(Config::FromEnv().database_url, {.pool_size = 4});
    if (!db.ok()) {
      GTEST_SKIP() << "database unavailable: " << db.status();
    }
    db_ = std::move(*db);
  }

  void TearDown() override {
    if (db_ == nullptr) return;
    for (const int64_t user_id : probe_user_ids_) {
      const DbParams params = {absl::StrCat(user_id)};
      EXPECT_OK(db_->Execute("DELETE FROM orders WHERE user_id = $1", params));
      EXPECT_OK(
          db_->Execute("DELETE FROM positions WHERE user_id = $1", params));
      EXPECT_OK(db_->Execute("DELETE FROM users WHERE id = $1", params));
    }
  }

  // A fresh user with the given cash; rows are removed in TearDown.
  int64_t CreateProbeUser(int64_t cash_cents) {
    const std::string username = absl::StrCat(
        "trading_probe_", absl::ToUnixMicros(absl::Now()));
    absl::StatusOr<Rows> rows = db_->Query(
        "INSERT INTO users (username, password_hash, cash_cents) "
        "VALUES ($1, 'probe', $2) RETURNING id",
        {username, absl::StrCat(cash_cents)});
    EXPECT_OK(rows);
    EXPECT_EQ(rows->size(), 1);
    const int64_t user_id = std::stoll(*(*rows)[0].columns[0]);
    probe_user_ids_.push_back(user_id);
    return user_id;
  }

  int64_t QueryInt(const std::string& sql, int64_t user_id) {
    absl::StatusOr<Rows> rows = db_->Query(sql, {absl::StrCat(user_id)});
    EXPECT_OK(rows);
    EXPECT_EQ(rows->size(), 1);
    if (!rows.ok() || rows->empty() || !(*rows)[0].columns[0].has_value()) {
      return -1;
    }
    return std::stoll(*(*rows)[0].columns[0]);
  }

  int64_t CashCents(int64_t user_id) {
    return QueryInt("SELECT cash_cents FROM users WHERE id = $1", user_id);
  }
  int64_t OrderCount(int64_t user_id) {
    return QueryInt("SELECT count(*) FROM orders WHERE user_id = $1", user_id);
  }
  int64_t PositionCount(int64_t user_id) {
    return QueryInt("SELECT count(*) FROM positions WHERE user_id = $1",
                    user_id);
  }
  int64_t LedgerSum(int64_t user_id) {
    return QueryInt(
        "SELECT coalesce(sum(cash_delta_cents), 0) FROM orders "
        "WHERE user_id = $1",
        user_id);
  }

  std::unique_ptr<Db> db_;
  std::vector<int64_t> probe_user_ids_;
};

TEST_F(TradingRepoIntegrationTest, FullLifecycleWithExactCents) {
  const int64_t user_id = CreateProbeUser(1000000);
  TradingRepo repo(*db_);

  // Buy 2 AAPL @ $105.0000: exact cents, ceil'd debit 21,000c.
  absl::StatusOr<OrderResult> buy1 =
      repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kBuy, 2, 1050000);
  ASSERT_OK(buy1);
  EXPECT_EQ(buy1->cash_delta_cents, -21000);
  EXPECT_EQ(buy1->cash_cents, 979000);
  EXPECT_EQ(buy1->position_quantity, 2);

  // Buy 1 more @ $105.0002: sub-cent debit ceils to 10,501c and the
  // weighted average lands on $105.0001 exactly.
  absl::StatusOr<OrderResult> buy2 =
      repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kBuy, 1, 1050002);
  ASSERT_OK(buy2);
  EXPECT_EQ(buy2->cash_delta_cents, -10501);
  EXPECT_EQ(buy2->cash_cents, 968499);
  EXPECT_EQ(buy2->position_quantity, 3);
  EXPECT_EQ(buy2->position_avg_price_e4, 1050001);
  {
    absl::StatusOr<Rows> rows = db_->Query(
        "SELECT avg_price FROM positions WHERE user_id = $1 AND "
        "symbol = 'AAPL'",
        {absl::StrCat(user_id)});
    ASSERT_OK(rows);
    ASSERT_EQ(rows->size(), 1);
    EXPECT_EQ((*rows)[0].columns[0], "105.0001");
  }

  // Sell 1 @ $105.0001: credit floors to 10,500c; avg price unchanged.
  absl::StatusOr<OrderResult> sell1 =
      repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kSell, 1, 1050001);
  ASSERT_OK(sell1);
  EXPECT_EQ(sell1->cash_delta_cents, 10500);
  EXPECT_EQ(sell1->cash_cents, 978999);
  EXPECT_EQ(sell1->position_avg_price_e4, 1050001);

  // Sell the remaining 2 @ $100: the position row disappears.
  absl::StatusOr<OrderResult> sell2 =
      repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kSell, 2, 1000000);
  ASSERT_OK(sell2);
  EXPECT_EQ(sell2->cash_delta_cents, 20000);
  EXPECT_EQ(sell2->cash_cents, 998999);
  EXPECT_EQ(sell2->position_quantity, 0);
  EXPECT_EQ(PositionCount(user_id), 0);

  // Short 1 TSLA @ $250, then cover @ $250.0001 (ceil'd): flat again.
  absl::StatusOr<OrderResult> short1 =
      repo.ExecuteOrder(user_id, Sym("TSLA"), OrderSide::kShort, 1, 2500000);
  ASSERT_OK(short1);
  EXPECT_EQ(short1->cash_delta_cents, 25000);
  EXPECT_EQ(short1->position_quantity, -1);
  absl::StatusOr<OrderResult> cover1 =
      repo.ExecuteOrder(user_id, Sym("TSLA"), OrderSide::kCover, 1, 2500001);
  ASSERT_OK(cover1);
  EXPECT_EQ(cover1->cash_delta_cents, -25001);
  EXPECT_EQ(cover1->cash_cents, 998998);
  EXPECT_EQ(PositionCount(user_id), 0);

  // The ledger invariant: cash == initial + sum of order deltas.
  EXPECT_EQ(OrderCount(user_id), 6);
  EXPECT_EQ(LedgerSum(user_id), -1002);
  EXPECT_EQ(CashCents(user_id), 998998);

  // The portfolio read sees the same snapshot.
  absl::StatusOr<Portfolio> portfolio = repo.GetPortfolio(user_id);
  ASSERT_OK(portfolio);
  EXPECT_EQ(portfolio->cash_cents, 998998);
  EXPECT_TRUE(portfolio->positions.empty());
}

TEST_F(TradingRepoIntegrationTest, EveryRejectionLeavesTheAccountUntouched) {
  const int64_t user_id = CreateProbeUser(1000);
  TradingRepo repo(*db_);

  // Shape, direction, cash, and margin rejections in turn.
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kBuy, 0,
                                1000000),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kSell, 1,
                                1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kCover, 1,
                                1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kBuy, 1,
                                600000),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  // Short 1 @ $100: 4 * 11,000 < 5 * 10,000.
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("AAPL"), OrderSide::kShort, 1,
                                1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_EQ(CashCents(user_id), 1000);
  EXPECT_EQ(OrderCount(user_id), 0);
  EXPECT_EQ(PositionCount(user_id), 0);
}

TEST_F(TradingRepoIntegrationTest, MidTransactionFailureRollsBackEverything) {
  const int64_t user_id = CreateProbeUser(1000);
  TradingRepo repo(*db_);

  // Valid shape, absent from instruments: the plan is accepted, then the
  // order INSERT hits the foreign key and the whole transaction rolls back.
  EXPECT_THAT(repo.ExecuteOrder(user_id, Sym("ZZZZZZ"), OrderSide::kBuy, 1,
                                10000),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(CashCents(user_id), 1000);
  EXPECT_EQ(OrderCount(user_id), 0);
  EXPECT_EQ(PositionCount(user_id), 0);
}

// Latch-released ExecuteOrder for the concurrency tests below.
void RunOrder(absl::Notification* start, TradingRepo* repo, int64_t user_id,
              const Symbol symbol, absl::StatusOr<OrderResult>* result) {
  start->WaitForNotification();
  *result = repo->ExecuteOrder(user_id, symbol, OrderSide::kBuy, 1, 600000);
}

TEST_F(TradingRepoIntegrationTest, ConcurrentOrdersSerializeOnTheUserRow) {
  // Cash covers one $60 buy, not two. The loser must block on FOR UPDATE
  // and revalidate against the winner's committed cash.
  const int64_t user_id = CreateProbeUser(10000);
  TradingRepo repo(*db_);
  absl::Notification start;
  absl::StatusOr<OrderResult> first;
  absl::StatusOr<OrderResult> second;
  std::thread thread1(RunOrder, &start, &repo, user_id, Sym("AAPL"), &first);
  std::thread thread2(RunOrder, &start, &repo, user_id, Sym("AAPL"), &second);
  start.Notify();
  thread1.join();
  thread2.join();

  const int successes = (first.ok() ? 1 : 0) + (second.ok() ? 1 : 0);
  EXPECT_EQ(successes, 1);
  const absl::Status& loser =
      first.ok() ? second.status() : first.status();
  EXPECT_THAT(loser, StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_EQ(CashCents(user_id), 4000);
  EXPECT_EQ(OrderCount(user_id), 1);
  EXPECT_EQ(QueryInt("SELECT quantity FROM positions WHERE user_id = $1",
                     user_id),
            1);
}

// Five $1 buy attempts, counting successes; failures must be the cash check.
void RunBuyLoop(absl::Notification* start, TradingRepo* repo, int64_t user_id,
                const Symbol symbol, std::atomic<int>* successes) {
  start->WaitForNotification();
  for (int i = 0; i < 5; ++i) {
    const absl::StatusOr<OrderResult> result =
        repo->ExecuteOrder(user_id, symbol, OrderSide::kBuy, 1, 10000);
    if (result.ok()) {
      ++*successes;
    } else {
      EXPECT_THAT(result, StatusIs(absl::StatusCode::kFailedPrecondition));
    }
  }
}

TEST_F(TradingRepoIntegrationTest, StressConservesTheLedgerInvariants) {
  // 700c of cash, 20 concurrent $1 buys: exactly 7 can succeed, and every
  // conserved quantity must agree regardless of interleaving.
  const int64_t user_id = CreateProbeUser(700);
  TradingRepo repo(*db_);
  absl::Notification start;
  std::atomic<int> successes = 0;
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(RunBuyLoop, &start, &repo, user_id, Sym("AAPL"),
                         &successes);
  }
  start.Notify();
  for (std::thread& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(successes.load(), 7);
  EXPECT_EQ(OrderCount(user_id), 7);
  EXPECT_EQ(QueryInt("SELECT quantity FROM positions WHERE user_id = $1",
                     user_id),
            7);
  EXPECT_EQ(CashCents(user_id), 0);
  EXPECT_EQ(LedgerSum(user_id), -700);
}

}  // namespace
}  // namespace firefly
