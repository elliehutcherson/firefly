#ifndef FIREFLY_TESTS_FAKES_TRADING_FAKE_TRADING_STORE_H_
#define FIREFLY_TESTS_FAKES_TRADING_FAKE_TRADING_STORE_H_

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/common/symbol.h"
#include "src/trading/order_math.h"
#include "src/trading/trading_store.h"

namespace firefly {

// One order recorded by FakeTradingStore.
struct FakeExecuteOrderCall {
  int64_t user_id = 0;
  std::string symbol;
  OrderSide side = OrderSide::kBuy;
  int64_t quantity = 0;
  int64_t price_e4 = 0;
};

// Replays canned results and records every call, like FakeDb.
class FakeTradingStore : public TradingStore {
 public:
  absl::StatusOr<OrderResult> ExecuteOrder(int64_t user_id,
                                           const Symbol& symbol,
                                           OrderSide side, int64_t quantity,
                                           int64_t price_e4) override {
    execute_calls.push_back(
        {user_id, symbol.str(), side, quantity, price_e4});
    if (order_results.empty()) {
      return absl::InternalError("FakeTradingStore: no order result left");
    }
    absl::StatusOr<OrderResult> result = std::move(order_results.front());
    order_results.pop_front();
    return result;
  }

  absl::StatusOr<Portfolio> GetPortfolio(int64_t user_id) override {
    portfolio_calls.push_back(user_id);
    if (portfolio_results.empty()) {
      return absl::InternalError("FakeTradingStore: no portfolio left");
    }
    absl::StatusOr<Portfolio> result = std::move(portfolio_results.front());
    portfolio_results.pop_front();
    return result;
  }

  std::vector<FakeExecuteOrderCall> execute_calls;
  std::vector<int64_t> portfolio_calls;
  std::deque<absl::StatusOr<OrderResult>> order_results;
  std::deque<absl::StatusOr<Portfolio>> portfolio_results;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_TRADING_FAKE_TRADING_STORE_H_
