#include "trading/order_math.h"

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/money.h"
#include "common/status_macros.h"

namespace firefly {
namespace {

absl::StatusOr<int64_t> CheckedAdd(int64_t a, int64_t b) {
  int64_t sum = 0;
  if (__builtin_add_overflow(a, b, &sum)) {
    return absl::InternalError(absl::StrCat("int64 overflow: ", a, " + ", b));
  }
  return sum;
}

// Debit/CreditCents flag bad inputs and overflow as InvalidArgument; inside
// order math (post shape validation) either is an invariant breach.
absl::StatusOr<int64_t> InternalDebit(int64_t price_e4, int64_t quantity) {
  absl::StatusOr<int64_t> cents = DebitCents(price_e4, quantity);
  if (!cents.ok()) return absl::InternalError(cents.status().message());
  return cents;
}

absl::StatusOr<int64_t> InternalCredit(int64_t price_e4, int64_t quantity) {
  absl::StatusOr<int64_t> cents = CreditCents(price_e4, quantity);
  if (!cents.ok()) return absl::InternalError(cents.status().message());
  return cents;
}

// Weighted average price over magnitudes, rounded half-away-from-zero to e4.
// old_magnitude may be 0 (opening a position); the result is then price_e4.
absl::StatusOr<int64_t> WeightedAvgE4(int64_t old_magnitude, int64_t old_avg_e4,
                                      int64_t quantity, int64_t price_e4) {
  int64_t old_cost = 0;
  int64_t added_cost = 0;
  int64_t numerator = 0;
  if (__builtin_mul_overflow(old_magnitude, old_avg_e4, &old_cost) ||
      __builtin_mul_overflow(quantity, price_e4, &added_cost) ||
      __builtin_add_overflow(old_cost, added_cost, &numerator)) {
    return absl::InternalError("weighted average price overflows int64");
  }
  ASSIGN_OR_RETURN(const int64_t denominator,
                   CheckedAdd(old_magnitude, quantity));
  ASSIGN_OR_RETURN(const int64_t rounded_numerator,
                   CheckedAdd(numerator, denominator / 2));
  return rounded_numerator / denominator;
}

// 4*cash' >= 5*exposure', the integer form of equity >= 25% of short
// exposure.
absl::Status CheckMargin(int64_t new_cash_cents, int64_t exposure_cents) {
  int64_t lhs = 0;
  int64_t rhs = 0;
  if (__builtin_mul_overflow(new_cash_cents, int64_t{4}, &lhs) ||
      __builtin_mul_overflow(exposure_cents, int64_t{5}, &rhs)) {
    return absl::InternalError("margin check overflows int64");
  }
  if (lhs < rhs) {
    return absl::FailedPreconditionError("insufficient margin for this order");
  }
  return absl::OkStatus();
}

absl::Status CheckDirectionAndQuantity(int64_t held, OrderSide side,
                                       int64_t quantity) {
  switch (side) {
    case OrderSide::kBuy:
      if (held < 0) {
        return absl::FailedPreconditionError(
            "cannot buy while short; cover the position first");
      }
      return absl::OkStatus();
    case OrderSide::kSell:
      if (held <= 0) {
        return absl::FailedPreconditionError("no long position to sell");
      }
      if (quantity > held) {
        return absl::FailedPreconditionError(
            "sell quantity exceeds shares held");
      }
      return absl::OkStatus();
    case OrderSide::kShort:
      if (held > 0) {
        return absl::FailedPreconditionError(
            "cannot short while long; sell the position first");
      }
      return absl::OkStatus();
    case OrderSide::kCover:
      if (held >= 0) {
        return absl::FailedPreconditionError("no short position to cover");
      }
      if (quantity > -held) {
        return absl::FailedPreconditionError(
            "cover quantity exceeds shares short");
      }
      return absl::OkStatus();
  }
  return absl::InternalError("unknown order side");
}

absl::StatusOr<OrderPlan> PlanBuy(const AccountView& account, int64_t quantity,
                                  int64_t price_e4) {
  ASSIGN_OR_RETURN(const int64_t debit, InternalDebit(price_e4, quantity));
  ASSIGN_OR_RETURN(const int64_t new_cash,
                   CheckedAdd(account.cash_cents, -debit));
  ASSIGN_OR_RETURN(const int64_t new_quantity,
                   CheckedAdd(account.position_quantity, quantity));
  ASSIGN_OR_RETURN(
      const int64_t new_avg,
      WeightedAvgE4(account.position_quantity, account.position_avg_price_e4,
                    quantity, price_e4));
  if (new_cash < 0) {
    return absl::FailedPreconditionError("insufficient cash");
  }
  RETURN_IF_ERROR(CheckMargin(new_cash, account.other_short_exposure_cents));
  return OrderPlan{.cash_delta_cents = -debit,
                   .new_cash_cents = new_cash,
                   .new_position_quantity = new_quantity,
                   .new_position_avg_price_e4 = new_avg};
}

absl::StatusOr<OrderPlan> PlanSell(const AccountView& account, int64_t quantity,
                                   int64_t price_e4) {
  ASSIGN_OR_RETURN(const int64_t credit, InternalCredit(price_e4, quantity));
  ASSIGN_OR_RETURN(const int64_t new_cash,
                   CheckedAdd(account.cash_cents, credit));
  return OrderPlan{.cash_delta_cents = credit,
                   .new_cash_cents = new_cash,
                   .new_position_quantity = account.position_quantity - quantity,
                   .new_position_avg_price_e4 = account.position_avg_price_e4};
}

absl::StatusOr<OrderPlan> PlanShort(const AccountView& account,
                                    int64_t quantity, int64_t price_e4) {
  ASSIGN_OR_RETURN(const int64_t credit, InternalCredit(price_e4, quantity));
  ASSIGN_OR_RETURN(const int64_t new_cash,
                   CheckedAdd(account.cash_cents, credit));
  ASSIGN_OR_RETURN(const int64_t new_quantity,
                   CheckedAdd(account.position_quantity, -quantity));
  ASSIGN_OR_RETURN(
      const int64_t new_avg,
      WeightedAvgE4(-account.position_quantity, account.position_avg_price_e4,
                    quantity, price_e4));
  ASSIGN_OR_RETURN(const int64_t own_exposure,
                   ShortExposureCents(new_quantity, new_avg));
  ASSIGN_OR_RETURN(
      const int64_t exposure,
      CheckedAdd(account.other_short_exposure_cents, own_exposure));
  RETURN_IF_ERROR(CheckMargin(new_cash, exposure));
  return OrderPlan{.cash_delta_cents = credit,
                   .new_cash_cents = new_cash,
                   .new_position_quantity = new_quantity,
                   .new_position_avg_price_e4 = new_avg};
}

absl::StatusOr<OrderPlan> PlanCover(const AccountView& account,
                                    int64_t quantity, int64_t price_e4) {
  ASSIGN_OR_RETURN(const int64_t debit, InternalDebit(price_e4, quantity));
  ASSIGN_OR_RETURN(const int64_t new_cash,
                   CheckedAdd(account.cash_cents, -debit));
  if (new_cash < 0) {
    return absl::FailedPreconditionError("insufficient cash");
  }
  return OrderPlan{.cash_delta_cents = -debit,
                   .new_cash_cents = new_cash,
                   .new_position_quantity = account.position_quantity + quantity,
                   .new_position_avg_price_e4 = account.position_avg_price_e4};
}

}  // namespace

absl::StatusOr<OrderSide> ParseOrderSide(absl::string_view raw) {
  if (raw == "buy") return OrderSide::kBuy;
  if (raw == "sell") return OrderSide::kSell;
  if (raw == "short") return OrderSide::kShort;
  if (raw == "cover") return OrderSide::kCover;
  return absl::InvalidArgumentError(
      absl::StrCat("side must be one of buy, sell, short, cover: '", raw, "'"));
}

absl::string_view OrderSideName(OrderSide side) {
  switch (side) {
    case OrderSide::kBuy:
      return "buy";
    case OrderSide::kSell:
      return "sell";
    case OrderSide::kShort:
      return "short";
    case OrderSide::kCover:
      return "cover";
  }
  return "unknown";
}

absl::StatusOr<OrderPlan> PlanOrder(const AccountView& account, OrderSide side,
                                    int64_t quantity, int64_t price_e4) {
  if (quantity < 1 || quantity > kMaxOrderQuantity) {
    return absl::InvalidArgumentError(absl::StrCat(
        "quantity must be a whole number between 1 and ", kMaxOrderQuantity));
  }
  if (price_e4 <= 0) {
    return absl::InvalidArgumentError("price must be positive");
  }
  RETURN_IF_ERROR(
      CheckDirectionAndQuantity(account.position_quantity, side, quantity));
  switch (side) {
    case OrderSide::kBuy:
      return PlanBuy(account, quantity, price_e4);
    case OrderSide::kSell:
      return PlanSell(account, quantity, price_e4);
    case OrderSide::kShort:
      return PlanShort(account, quantity, price_e4);
    case OrderSide::kCover:
      return PlanCover(account, quantity, price_e4);
  }
  return absl::InternalError("unknown order side");
}

absl::StatusOr<int64_t> ShortExposureCents(int64_t quantity,
                                           int64_t avg_price_e4) {
  const int64_t magnitude = quantity < 0 ? -quantity : quantity;
  absl::StatusOr<int64_t> cents = DebitCents(avg_price_e4, magnitude);
  if (!cents.ok()) return absl::InternalError(cents.status().message());
  return cents;
}

}  // namespace firefly
