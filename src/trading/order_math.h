#ifndef FIREFLY_TRADING_ORDER_MATH_H_
#define FIREFLY_TRADING_ORDER_MATH_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace firefly {

// Pure order semantics: given an account snapshot and an order, decide
// acceptance and compute the exact post-trade numbers. No I/O — TradingRepo
// runs this between its SELECTs and its writes, and tests pin every rule
// here hermetically.
//
// Rules (docs plan, Milestone 4):
//   * Orders never cross zero: sell at most what is held, cover at most the
//     short size; flipping direction takes two orders.
//   * Debits (buy, cover) round sub-cent costs up, credits (sell, short)
//     round down — see DebitCents/CreditCents.
//   * avg_price is the weighted average over magnitudes, rounded
//     half-away-from-zero to e4; it never feeds cash.
//   * Margin v1: buy and short require post-trade 4*cash' >= 5*exposure',
//     where exposure' sums short legs at their average-cost marks (equity
//     >= 25% of short exposure; longs ignored, which is stricter). Buy also
//     requires cash' >= 0; cover requires only cash' >= 0 (risk-reducing);
//     sell is unchecked.
//
// Validation order is fixed and observable: shape (InvalidArgument) ->
// direction -> quantity-vs-held -> compute -> cash' >= 0 -> margin (all
// FailedPrecondition, messages client-safe). Arithmetic overflow is an
// invariant breach -> Internal; unreachable on valid data given the
// quantity cap and the users_cash_cents_bounds_check migration.

enum class OrderSide { kBuy, kSell, kShort, kCover };

// Exact lowercase JSON values: "buy", "sell", "short", "cover".
absl::StatusOr<OrderSide> ParseOrderSide(absl::string_view raw);
absl::string_view OrderSideName(OrderSide side);

inline constexpr int64_t kMaxOrderQuantity = 1000000;

// Account state for one candidate order, as read under the users-row lock.
struct AccountView {
  int64_t cash_cents = 0;
  // This symbol's position: 0 = flat, negative = short.
  int64_t position_quantity = 0;
  int64_t position_avg_price_e4 = 0;  // 0 when flat
  // Sum of ShortExposureCents over every OTHER symbol's short position.
  int64_t other_short_exposure_cents = 0;
};

// The accepted order's exact effects. new_position_quantity == 0 means the
// position row must be DELETEd (the schema rejects zero-quantity rows).
struct OrderPlan {
  int64_t cash_delta_cents = 0;  // negative for buy/cover, positive for sell/short
  int64_t new_cash_cents = 0;
  int64_t new_position_quantity = 0;
  int64_t new_position_avg_price_e4 = 0;
};

absl::StatusOr<OrderPlan> PlanOrder(const AccountView& account, OrderSide side,
                                    int64_t quantity, int64_t price_e4);

// A short position's margin exposure: ceil(|quantity| * avg_price_e4 / 100)
// cents. Accepts the stored (negative) quantity. Inputs come from the
// database, so invalid ones are an invariant breach -> Internal.
absl::StatusOr<int64_t> ShortExposureCents(int64_t quantity,
                                           int64_t avg_price_e4);

}  // namespace firefly

#endif  // FIREFLY_TRADING_ORDER_MATH_H_
