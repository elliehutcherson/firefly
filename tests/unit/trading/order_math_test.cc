#include "src/trading/order_math.h"

#include <cstdint>
#include <limits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

// Unpacks a plan the test expects to be accepted.
OrderPlan MustPlan(const AccountView& account, OrderSide side, int64_t quantity,
                   int64_t price_e4) {
  absl::StatusOr<OrderPlan> plan =
      PlanOrder(account, side, quantity, price_e4);
  EXPECT_OK(plan);
  return plan.value_or(OrderPlan{});
}

TEST(ParseOrderSideTest, RoundTripsEverySide) {
  for (const OrderSide side : {OrderSide::kBuy, OrderSide::kSell,
                               OrderSide::kShort, OrderSide::kCover}) {
    EXPECT_THAT(ParseOrderSide(OrderSideName(side)), IsOkAndHolds(side));
  }
}

TEST(ParseOrderSideTest, RejectsAnythingElse) {
  for (const char* junk : {"", "BUY", "Buy", "hold", "buy ", "cover1"}) {
    EXPECT_THAT(ParseOrderSide(junk),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "input: '" << junk << "'";
  }
}

TEST(PlanOrderTest, RejectsMalformedShape) {
  const AccountView flat{.cash_cents = 1000000};
  for (const int64_t quantity : {int64_t{0}, int64_t{-1}, kMaxOrderQuantity + 1}) {
    EXPECT_THAT(PlanOrder(flat, OrderSide::kBuy, quantity, 10000),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "quantity: " << quantity;
  }
  for (const int64_t price_e4 : {int64_t{0}, int64_t{-10000}}) {
    EXPECT_THAT(PlanOrder(flat, OrderSide::kBuy, 1, price_e4),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "price_e4: " << price_e4;
  }
}

TEST(PlanOrderTest, MaxQuantityIsValid) {
  const AccountView account{.cash_cents = 1000000};
  // 1,000,000 shares at $0.01 costs exactly $10,000.
  const OrderPlan plan =
      MustPlan(account, OrderSide::kBuy, kMaxOrderQuantity, 100);
  EXPECT_EQ(plan.cash_delta_cents, -1000000);
  EXPECT_EQ(plan.new_cash_cents, 0);
  EXPECT_EQ(plan.new_position_quantity, kMaxOrderQuantity);
}

// --- Buy ---

TEST(PlanOrderTest, BuyFromFlatCeilsTheDebit) {
  const AccountView account{.cash_cents = 1000000};
  // 2 x $189.9550 = $379.91 exactly.
  const OrderPlan exact = MustPlan(account, OrderSide::kBuy, 2, 1899550);
  EXPECT_EQ(exact.cash_delta_cents, -37991);
  EXPECT_EQ(exact.new_cash_cents, 962009);
  EXPECT_EQ(exact.new_position_quantity, 2);
  EXPECT_EQ(exact.new_position_avg_price_e4, 1899550);

  // 3 x $10.0001 = $30.0003 rounds up to $30.01 against the buyer.
  const OrderPlan sub_cent = MustPlan(account, OrderSide::kBuy, 3, 100001);
  EXPECT_EQ(sub_cent.cash_delta_cents, -3001);
}

TEST(PlanOrderTest, BuyWeightedAverageRoundsHalfUp) {
  // 1 @ $105.0000 + 1 @ $105.0002: the mean $105.0001 is exact.
  const AccountView account{.cash_cents = 100000000,
                            .position_quantity = 1,
                            .position_avg_price_e4 = 1050000};
  const OrderPlan plan = MustPlan(account, OrderSide::kBuy, 1, 1050002);
  EXPECT_EQ(plan.new_position_quantity, 2);
  EXPECT_EQ(plan.new_position_avg_price_e4, 1050001);

  // 1 @ $100.0000 + 1 @ $100.0001: the .5 e4 tie rounds up.
  const AccountView tie{.cash_cents = 100000000,
                        .position_quantity = 1,
                        .position_avg_price_e4 = 1000000};
  EXPECT_EQ(MustPlan(tie, OrderSide::kBuy, 1, 1000001)
                .new_position_avg_price_e4,
            1000001);
}

TEST(PlanOrderTest, BuyWhileShortIsRejected) {
  const AccountView account{.cash_cents = 1000000,
                            .position_quantity = -1,
                            .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 1, 10000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("cover")));
}

TEST(PlanOrderTest, BuyRequiresCash) {
  const AccountView account{.cash_cents = 100};
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 1, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("insufficient cash")));
}

TEST(PlanOrderTest, BuyCannotDrainTheMarginCushion) {
  // Other shorts expose 50,000c, so post-trade cash must stay >= 62,500c.
  const AccountView account{.cash_cents = 100000,
                            .other_short_exposure_cents = 50000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 1, 4000000),  // $400 debit
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("margin")));
  const OrderPlan ok = MustPlan(account, OrderSide::kBuy, 1, 2000000);  // $200
  EXPECT_EQ(ok.new_cash_cents, 80000);
}

TEST(PlanOrderTest, BuyMarginBoundaryIsInclusive) {
  // 4 * 50,000 == 5 * 40,000 passes; one cent more debit fails.
  const AccountView account{.cash_cents = 100000,
                            .other_short_exposure_cents = 40000};
  EXPECT_OK(PlanOrder(account, OrderSide::kBuy, 1, 5000000));
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 1, 5000100),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("margin")));
}

// --- Sell ---

TEST(PlanOrderTest, SellFloorsTheCreditAndKeepsAvgPrice) {
  const AccountView account{.cash_cents = 0,
                            .position_quantity = 2,
                            .position_avg_price_e4 = 1899550};
  // 1 x $189.9555 = $189.9555 floors to $189.95.
  const OrderPlan plan = MustPlan(account, OrderSide::kSell, 1, 1899555);
  EXPECT_EQ(plan.cash_delta_cents, 18995);
  EXPECT_EQ(plan.new_cash_cents, 18995);
  EXPECT_EQ(plan.new_position_quantity, 1);
  EXPECT_EQ(plan.new_position_avg_price_e4, 1899550);
}

TEST(PlanOrderTest, SellToZeroDeletesThePosition) {
  const AccountView account{.cash_cents = 0,
                            .position_quantity = 2,
                            .position_avg_price_e4 = 1000000};
  EXPECT_EQ(MustPlan(account, OrderSide::kSell, 2, 1000000)
                .new_position_quantity,
            0);
}

TEST(PlanOrderTest, SellCannotCrossZero) {
  const AccountView account{.cash_cents = 0,
                            .position_quantity = 2,
                            .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kSell, 3, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("exceeds shares held")));
}

TEST(PlanOrderTest, SellNeedsALongPosition) {
  for (const int64_t held : {int64_t{0}, int64_t{-2}}) {
    const AccountView account{.cash_cents = 1000,
                              .position_quantity = held,
                              .position_avg_price_e4 = held == 0 ? 0 : 1000000};
    EXPECT_THAT(PlanOrder(account, OrderSide::kSell, 1, 1000000),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("no long position")))
        << "held: " << held;
  }
}

TEST(PlanOrderTest, SellIsUncheckedAgainstCashAndMargin) {
  // Even a deeply negative account (possible after M5 force-cover) can
  // always reduce risk by selling.
  const AccountView account{.cash_cents = -50000,
                            .position_quantity = 1,
                            .position_avg_price_e4 = 1000000,
                            .other_short_exposure_cents = 900000};
  const OrderPlan plan = MustPlan(account, OrderSide::kSell, 1, 1000000);
  EXPECT_EQ(plan.new_cash_cents, -40000);
  EXPECT_EQ(plan.new_position_quantity, 0);
}

// --- Short ---

TEST(PlanOrderTest, ShortMarginBoundaryIsInclusive) {
  // Short 1 @ $100: credit 10,000c, own exposure 10,000c. With $25.00 cash
  // 4 * 12,500 == 5 * 10,000 passes exactly; a cent less fails.
  const AccountView at_boundary{.cash_cents = 2500};
  const OrderPlan plan = MustPlan(at_boundary, OrderSide::kShort, 1, 1000000);
  EXPECT_EQ(plan.cash_delta_cents, 10000);
  EXPECT_EQ(plan.new_cash_cents, 12500);
  EXPECT_EQ(plan.new_position_quantity, -1);
  EXPECT_EQ(plan.new_position_avg_price_e4, 1000000);

  const AccountView below{.cash_cents = 2499};
  EXPECT_THAT(PlanOrder(below, OrderSide::kShort, 1, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("margin")));
}

TEST(PlanOrderTest, ShortCreditIsFloored) {
  // 3 x $10.0001 = $30.0003 floors to $30.00 for the seller.
  const AccountView account{.cash_cents = 100000};
  EXPECT_EQ(MustPlan(account, OrderSide::kShort, 3, 100001).cash_delta_cents,
            3000);
}

TEST(PlanOrderTest, ShortAddsOnMagnitudesAndCeilsExposure) {
  // -1 @ $100.0000 short 1 more @ $100.0001: avg $100.00005 rounds (on
  // magnitudes, half away from zero) to $100.0001; exposure
  // ceil(2 * 1000001 / 100) = 20,001c.
  const AccountView account{.cash_cents = 20000,
                            .position_quantity = -1,
                            .position_avg_price_e4 = 1000000};
  const OrderPlan plan = MustPlan(account, OrderSide::kShort, 1, 1000001);
  EXPECT_EQ(plan.new_position_quantity, -2);
  EXPECT_EQ(plan.new_position_avg_price_e4, 1000001);
  EXPECT_EQ(plan.new_cash_cents, 30000);
}

TEST(PlanOrderTest, ShortCountsOtherShortExposure) {
  // Alone this short passes at the boundary; other shorts' exposure tips it.
  const AccountView account{.cash_cents = 2500,
                            .other_short_exposure_cents = 1};
  EXPECT_THAT(PlanOrder(account, OrderSide::kShort, 1, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("margin")));
}

TEST(PlanOrderTest, ShortWhileLongIsRejected) {
  const AccountView account{.cash_cents = 1000000,
                            .position_quantity = 1,
                            .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kShort, 1, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("sell")));
}

// --- Cover ---

TEST(PlanOrderTest, CoverCeilsTheDebitAndKeepsAvgPrice) {
  const AccountView account{.cash_cents = 100000,
                            .position_quantity = -2,
                            .position_avg_price_e4 = 1000000};
  // 1 x $100.0001 rounds up to $100.01.
  const OrderPlan plan = MustPlan(account, OrderSide::kCover, 1, 1000001);
  EXPECT_EQ(plan.cash_delta_cents, -10001);
  EXPECT_EQ(plan.new_cash_cents, 89999);
  EXPECT_EQ(plan.new_position_quantity, -1);
  EXPECT_EQ(plan.new_position_avg_price_e4, 1000000);
}

TEST(PlanOrderTest, CoverToZeroDeletesThePosition) {
  const AccountView account{.cash_cents = 100000,
                            .position_quantity = -2,
                            .position_avg_price_e4 = 1000000};
  EXPECT_EQ(MustPlan(account, OrderSide::kCover, 2, 1000000)
                .new_position_quantity,
            0);
}

TEST(PlanOrderTest, CoverCannotCrossZero) {
  const AccountView account{.cash_cents = 1000000,
                            .position_quantity = -2,
                            .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kCover, 3, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("exceeds shares short")));
}

TEST(PlanOrderTest, CoverNeedsAShortPosition) {
  for (const int64_t held : {int64_t{0}, int64_t{2}}) {
    const AccountView account{.cash_cents = 1000000,
                              .position_quantity = held,
                              .position_avg_price_e4 = held == 0 ? 0 : 1000000};
    EXPECT_THAT(PlanOrder(account, OrderSide::kCover, 1, 1000000),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("no short position")))
        << "held: " << held;
  }
}

TEST(PlanOrderTest, UnderwaterCoverOnlyNeedsCash) {
  // Short at $100, price now $150: equity is gone, but covering is
  // risk-reducing and allowed while cash covers the debit.
  const AccountView account{.cash_cents = 20000,
                            .position_quantity = -1,
                            .position_avg_price_e4 = 1000000};
  const OrderPlan plan = MustPlan(account, OrderSide::kCover, 1, 1500000);
  EXPECT_EQ(plan.new_cash_cents, 5000);
  EXPECT_EQ(plan.new_position_quantity, 0);

  const AccountView broke{.cash_cents = 10000,
                          .position_quantity = -1,
                          .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(broke, OrderSide::kCover, 1, 1500000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("insufficient cash")));
}

TEST(PlanOrderTest, PartialCoverIgnoresRemainingExposure) {
  // The remaining short's exposure would fail a margin check; cover only
  // requires cash' >= 0.
  const AccountView account{.cash_cents = 15000,
                            .position_quantity = -10,
                            .position_avg_price_e4 = 1000000};
  const OrderPlan plan = MustPlan(account, OrderSide::kCover, 1, 1400000);
  EXPECT_EQ(plan.new_cash_cents, 1000);
  EXPECT_EQ(plan.new_position_quantity, -9);
}

// --- Validation order ---

TEST(PlanOrderTest, ShapeIsCheckedBeforeDirection) {
  // A sell that is both malformed (qty 0) and wrong-direction (flat) fails
  // as InvalidArgument, not FailedPrecondition.
  const AccountView flat{.cash_cents = 1000};
  EXPECT_THAT(PlanOrder(flat, OrderSide::kSell, 0, 1000000),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PlanOrderTest, QuantityBoundIsCheckedBeforeCash) {
  // Crossing and insufficient cash at once: the crossing error wins.
  const AccountView account{.cash_cents = 0,
                            .position_quantity = -2,
                            .position_avg_price_e4 = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kCover, 3, 1000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("exceeds shares short")));
}

TEST(PlanOrderTest, CashIsCheckedBeforeMargin) {
  // Fails both cash' >= 0 and the margin check; the cash message wins.
  const AccountView account{.cash_cents = 10000,
                            .other_short_exposure_cents = 100000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 1, 2000000),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("insufficient cash")));
}

// --- Overflow and exposure ---

TEST(PlanOrderTest, OverflowIsAnInternalError) {
  const AccountView account{.cash_cents = 1000000};
  EXPECT_THAT(PlanOrder(account, OrderSide::kBuy, 2,
                        std::numeric_limits<int64_t>::max()),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(ShortExposureCentsTest, CeilsOnTheMagnitude) {
  EXPECT_THAT(ShortExposureCents(-3, 100001), IsOkAndHolds(3001));
  EXPECT_THAT(ShortExposureCents(-1, 1), IsOkAndHolds(1));
  EXPECT_THAT(ShortExposureCents(-2, 1000000), IsOkAndHolds(20000));
}

TEST(ShortExposureCentsTest, InvalidRowsAreInternalErrors) {
  EXPECT_THAT(ShortExposureCents(0, 1000000),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ShortExposureCents(-1, 0), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ShortExposureCents(-2, std::numeric_limits<int64_t>::max()),
              StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace firefly
