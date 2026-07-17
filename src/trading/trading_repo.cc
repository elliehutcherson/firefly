#include "src/trading/trading_repo.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/common/money.h"
#include "src/common/status_macros.h"
#include "src/common/symbol.h"
#include "src/db/row_reader.h"
#include "src/db/transaction.h"
#include "src/trading/order_math.h"

namespace firefly {
namespace {

// This symbol's current position; zero-initialized when no row exists.
struct PositionRow {
  int64_t quantity = 0;
  int64_t avg_price_e4 = 0;
};

// Database values that fail to parse are invariant breaches, not caller
// errors: the schema's CHECKs guarantee well-formed rows.
absl::StatusOr<int64_t> PriceE4FromDb(absl::string_view text) {
  absl::StatusOr<int64_t> price_e4 = PriceE4FromString(text);
  if (!price_e4.ok()) {
    return absl::InternalError(
        absl::StrCat("unparseable price in positions row: ", text));
  }
  return price_e4;
}

absl::StatusOr<int64_t> LockCash(SqlExecutor& executor, int64_t user_id) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      executor.Query("SELECT cash_cents FROM users WHERE id = $1 FOR UPDATE",
                     {absl::StrCat(user_id)}));
  if (rows.size() != 1) {
    return absl::InternalError(
        absl::StrCat("no users row to lock for user ", user_id));
  }
  return RowReader(rows[0], "users").Int64(0);
}

absl::StatusOr<PositionRow> ReadPosition(SqlExecutor& executor,
                                         int64_t user_id,
                                         const Symbol& symbol) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      executor.Query("SELECT quantity, avg_price FROM positions "
                     "WHERE user_id = $1 AND symbol = $2",
                     {absl::StrCat(user_id), symbol.str()}));
  if (rows.empty()) {
    return PositionRow{};
  }
  const RowReader row(rows[0], "positions");
  PositionRow position;
  ASSIGN_OR_RETURN(position.quantity, row.Int64(0));
  ASSIGN_OR_RETURN(const absl::string_view price, row.RequiredString(1));
  ASSIGN_OR_RETURN(position.avg_price_e4, PriceE4FromDb(price));
  return position;
}

// Margin exposure of every OTHER symbol's short position, summed in C++
// because the ceil-per-position rounding must match ShortExposureCents.
absl::StatusOr<int64_t> SumOtherShortExposure(SqlExecutor& executor,
                                              int64_t user_id,
                                              const Symbol& symbol) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      executor.Query("SELECT quantity, avg_price FROM positions "
                     "WHERE user_id = $1 AND quantity < 0 AND symbol <> $2",
                     {absl::StrCat(user_id), symbol.str()}));
  int64_t total_cents = 0;
  for (const Row& raw_row : rows) {
    const RowReader row(raw_row, "positions");
    ASSIGN_OR_RETURN(const int64_t quantity, row.Int64(0));
    ASSIGN_OR_RETURN(const absl::string_view price, row.RequiredString(1));
    ASSIGN_OR_RETURN(const int64_t avg_price_e4, PriceE4FromDb(price));
    ASSIGN_OR_RETURN(const int64_t exposure,
                     ShortExposureCents(quantity, avg_price_e4));
    if (__builtin_add_overflow(total_cents, exposure, &total_cents)) {
      return absl::InternalError("short exposure sum overflows int64");
    }
  }
  return total_cents;
}

absl::StatusOr<int64_t> InsertOrder(SqlExecutor& executor, int64_t user_id,
                                    const Symbol& symbol, OrderSide side,
                                    int64_t quantity, int64_t price_e4,
                                    int64_t cash_delta_cents) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      executor.Query(
          "INSERT INTO orders (user_id, symbol, side, quantity, price, "
          "cash_delta_cents) VALUES ($1, $2, $3, $4, $5, $6) RETURNING id",
          {absl::StrCat(user_id), symbol.str(),
           std::string(OrderSideName(side)), absl::StrCat(quantity),
           PriceE4ToString(price_e4), absl::StrCat(cash_delta_cents)}));
  if (rows.size() != 1) {
    return absl::InternalError("INSERT ... RETURNING id returned no row");
  }
  return RowReader(rows[0], "orders").Int64(0);
}

absl::Status UpdateCash(SqlExecutor& executor, int64_t user_id,
                        int64_t new_cash_cents) {
  ASSIGN_OR_RETURN(
      const int64_t affected,
      executor.Execute("UPDATE users SET cash_cents = $2 WHERE id = $1",
                       {absl::StrCat(user_id), absl::StrCat(new_cash_cents)}));
  if (affected != 1) {
    return absl::InternalError(
        absl::StrCat("cash update touched ", affected, " rows"));
  }
  return absl::OkStatus();
}

// INSERT, UPDATE, or DELETE the position row by old -> new quantity. No
// ON CONFLICT: under the users-row lock a conflicting insert is impossible,
// so a 23505 must surface as the bug it would be.
absl::Status WritePosition(SqlExecutor& executor, int64_t user_id,
                           const Symbol& symbol, int64_t old_quantity,
                           const OrderPlan& plan) {
  absl::StatusOr<int64_t> affected(0);
  if (plan.new_position_quantity == 0) {
    affected = executor.Execute(
        "DELETE FROM positions WHERE user_id = $1 AND symbol = $2",
        {absl::StrCat(user_id), symbol.str()});
  } else if (old_quantity == 0) {
    affected = executor.Execute(
        "INSERT INTO positions (user_id, symbol, quantity, avg_price) "
        "VALUES ($1, $2, $3, $4)",
        {absl::StrCat(user_id), symbol.str(),
         absl::StrCat(plan.new_position_quantity),
         PriceE4ToString(plan.new_position_avg_price_e4)});
  } else {
    affected = executor.Execute(
        "UPDATE positions SET quantity = $3, avg_price = $4, "
        "updated_at = now() WHERE user_id = $1 AND symbol = $2",
        {absl::StrCat(user_id), symbol.str(),
         absl::StrCat(plan.new_position_quantity),
         PriceE4ToString(plan.new_position_avg_price_e4)});
  }
  RETURN_IF_ERROR(affected.status());
  if (*affected != 1) {
    return absl::InternalError(
        absl::StrCat("position write touched ", *affected, " rows"));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<OrderResult> TradingRepo::ExecuteOrder(int64_t user_id,
                                                      const Symbol& symbol,
                                                      OrderSide side,
                                                      int64_t quantity,
                                                      int64_t price_e4) {
  ASSIGN_OR_RETURN(const std::unique_ptr<Transaction> transaction,
                   db_.Begin());
  AccountView account;
  ASSIGN_OR_RETURN(account.cash_cents, LockCash(*transaction, user_id));
  ASSIGN_OR_RETURN(const PositionRow position,
                   ReadPosition(*transaction, user_id, symbol));
  account.position_quantity = position.quantity;
  account.position_avg_price_e4 = position.avg_price_e4;
  if (side == OrderSide::kBuy || side == OrderSide::kShort) {
    ASSIGN_OR_RETURN(account.other_short_exposure_cents,
                     SumOtherShortExposure(*transaction, user_id, symbol));
  }
  ASSIGN_OR_RETURN(const OrderPlan plan,
                   PlanOrder(account, side, quantity, price_e4));
  ASSIGN_OR_RETURN(const int64_t order_id,
                   InsertOrder(*transaction, user_id, symbol, side, quantity,
                               price_e4, plan.cash_delta_cents));
  RETURN_IF_ERROR(UpdateCash(*transaction, user_id, plan.new_cash_cents));
  RETURN_IF_ERROR(
      WritePosition(*transaction, user_id, symbol, position.quantity, plan));
  RETURN_IF_ERROR(transaction->Commit());
  return OrderResult{.order_id = order_id,
                     .price_e4 = price_e4,
                     .cash_delta_cents = plan.cash_delta_cents,
                     .cash_cents = plan.new_cash_cents,
                     .position_quantity = plan.new_position_quantity,
                     .position_avg_price_e4 = plan.new_position_avg_price_e4};
}

absl::StatusOr<Portfolio> TradingRepo::GetPortfolio(int64_t user_id) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT u.cash_cents, p.symbol, p.quantity, p.avg_price "
                "FROM users u LEFT JOIN positions p ON p.user_id = u.id "
                "WHERE u.id = $1 ORDER BY p.symbol",
                {absl::StrCat(user_id)}));
  if (rows.empty()) {
    return absl::NotFoundError(absl::StrCat("no user ", user_id));
  }
  Portfolio portfolio;
  ASSIGN_OR_RETURN(portfolio.cash_cents,
                   RowReader(rows[0], "portfolio").Int64(0));
  for (const Row& raw_row : rows) {
    const RowReader row(raw_row, "portfolio");
    ASSIGN_OR_RETURN(const std::optional<absl::string_view> symbol_text,
                     row.OptionalString(1));
    if (!symbol_text.has_value()) {
      continue;  // the flat account's single all-NULL position row
    }
    absl::StatusOr<Symbol> symbol = Symbol::Parse(*symbol_text);
    if (!symbol.ok()) {
      return absl::InternalError(
          absl::StrCat("unparseable symbol in positions row: ", *symbol_text));
    }
    ASSIGN_OR_RETURN(const int64_t position_quantity, row.Int64(2));
    ASSIGN_OR_RETURN(const absl::string_view price, row.RequiredString(3));
    ASSIGN_OR_RETURN(const int64_t avg_price_e4, PriceE4FromDb(price));
    portfolio.positions.push_back(PositionRecord{
        .symbol = *std::move(symbol),
        .quantity = position_quantity,
        .avg_price_e4 = avg_price_e4});
  }
  return portfolio;
}

}  // namespace firefly
