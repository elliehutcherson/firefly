#ifndef FIREFLY_TRADING_TRADING_REPO_H_
#define FIREFLY_TRADING_TRADING_REPO_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "common/symbol.h"
#include "db/db.h"
#include "trading/order_math.h"
#include "trading/trading_store.h"

namespace firefly {

// Postgres-backed TradingStore. ExecuteOrder runs one transaction whose only
// lock is SELECT ... FOR UPDATE on the users row: every order touches cash,
// so that row is the single serialization point per user, and all position
// reads and writes under it cannot race. One lock means no deadlock ordering
// and no ON CONFLICT — a duplicate-position insert would be a bug and should
// surface as one. Any error before Commit() rolls the transaction back via
// the Transaction destructor, leaving the account untouched.
class TradingRepo : public TradingStore {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit TradingRepo(Db& db) : db_(db) {}

  absl::StatusOr<OrderResult> ExecuteOrder(int64_t user_id,
                                           const Symbol& symbol,
                                           OrderSide side, int64_t quantity,
                                           int64_t price_e4) override;

  // One LEFT JOIN statement, so cash and positions come from a single
  // consistent snapshot.
  absl::StatusOr<Portfolio> GetPortfolio(int64_t user_id) override;

 private:
  Db& db_;
};

}  // namespace firefly

#endif  // FIREFLY_TRADING_TRADING_REPO_H_
