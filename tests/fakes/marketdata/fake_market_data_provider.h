#ifndef FIREFLY_TESTS_FAKES_FAKE_MARKET_DATA_PROVIDER_H_
#define FIREFLY_TESTS_FAKES_FAKE_MARKET_DATA_PROVIDER_H_

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/marketdata/provider.h"

namespace firefly {

// One daily-bar request recorded by FakeMarketDataProvider.
struct FakeBarsCall {
  std::string symbol;
  absl::CivilDay start;
  absl::CivilDay end;
};

// One minute-bar request recorded by FakeMarketDataProvider.
struct FakeMinuteBarsCall {
  std::string symbol;
  absl::Time start;
  absl::Time end;
};

// Replays canned results and records every call, like FakeDb.
class FakeMarketDataProvider : public MarketDataProvider {
 public:
  absl::StatusOr<Trade> GetLatestTrade(const std::string& symbol) override {
    latest_trade_calls.push_back(symbol);
    if (latest_trade_results.empty()) {
      return absl::InternalError("FakeMarketDataProvider: no trade left");
    }
    absl::StatusOr<Trade> result = std::move(latest_trade_results.front());
    latest_trade_results.pop_front();
    return result;
  }

  absl::StatusOr<std::vector<Bar>> GetDailyBars(const std::string& symbol,
                                                absl::CivilDay start,
                                                absl::CivilDay end) override {
    daily_bars_calls.push_back({symbol, start, end});
    if (daily_bars_results.empty()) {
      return absl::InternalError("FakeMarketDataProvider: no daily bars left");
    }
    absl::StatusOr<std::vector<Bar>> result =
        std::move(daily_bars_results.front());
    daily_bars_results.pop_front();
    return result;
  }

  absl::StatusOr<std::vector<Bar>> GetMinuteBars(const std::string& symbol,
                                                 absl::Time start,
                                                 absl::Time end) override {
    minute_bars_calls.push_back({symbol, start, end});
    if (minute_bars_results.empty()) {
      return absl::InternalError("FakeMarketDataProvider: no minute bars left");
    }
    absl::StatusOr<std::vector<Bar>> result =
        std::move(minute_bars_results.front());
    minute_bars_results.pop_front();
    return result;
  }

  std::vector<std::string> latest_trade_calls;
  std::vector<FakeBarsCall> daily_bars_calls;
  std::vector<FakeMinuteBarsCall> minute_bars_calls;
  std::deque<absl::StatusOr<Trade>> latest_trade_results;
  std::deque<absl::StatusOr<std::vector<Bar>>> daily_bars_results;
  std::deque<absl::StatusOr<std::vector<Bar>>> minute_bars_results;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_MARKET_DATA_PROVIDER_H_
