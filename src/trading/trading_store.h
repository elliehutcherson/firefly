#ifndef FIREFLY_TRADING_TRADING_STORE_H_
#define FIREFLY_TRADING_TRADING_STORE_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "src/common/symbol.h"
#include "src/trading/order_math.h"

namespace firefly {

// The accepted order as committed: the ledger row's id and price, the cash
// movement, and the post-trade account state for the response body.
// position_quantity == 0 means the position closed (row deleted).
struct OrderResult {
  int64_t order_id = 0;
  int64_t price_e4 = 0;
  int64_t cash_delta_cents = 0;
  int64_t cash_cents = 0;
  int64_t position_quantity = 0;
  int64_t position_avg_price_e4 = 0;
};

struct PositionRecord {
  Symbol symbol;
  int64_t quantity = 0;  // negative = short
  int64_t avg_price_e4 = 0;
};

struct Portfolio {
  int64_t cash_cents = 0;
  std::vector<PositionRecord> positions;
};

// Trading's persistence boundary — one transaction-sized method per
// operation, so handlers never compose partial writes. ExecuteOrder either
// commits the full order effect (ledger row, cash, position) or leaves the
// account untouched: rejections come back as FailedPrecondition with a
// client-safe message and no side effects.
//
// The store is pure database work: callers resolve the execution price
// (real-time IEX quote) and market hours BEFORE calling, so no network I/O
// ever runs under the row lock.
class TradingStore {
 public:
  virtual ~TradingStore() = default;

  virtual absl::StatusOr<OrderResult> ExecuteOrder(int64_t user_id,
                                                   const Symbol& symbol,
                                                   OrderSide side,
                                                   int64_t quantity,
                                                   int64_t price_e4) = 0;

  // Cash and all open positions in one consistent snapshot; NotFound when
  // the user does not exist.
  virtual absl::StatusOr<Portfolio> GetPortfolio(int64_t user_id) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_TRADING_TRADING_STORE_H_
