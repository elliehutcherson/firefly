#ifndef FIREFLY_MARKETDATA_PROVIDER_H_
#define FIREFLY_MARKETDATA_PROVIDER_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "common/symbol.h"

namespace firefly {

// One OHLCV candle. Prices are e4 fixed point (src/common/money.h).
struct Bar {
  absl::Time time;
  int64_t open_e4 = 0;
  int64_t high_e4 = 0;
  int64_t low_e4 = 0;
  int64_t close_e4 = 0;
  int64_t volume = 0;
};

// The most recent sale of a symbol. Orders execute at this price.
struct Trade {
  int64_t price_e4 = 0;
  absl::Time time;
};

// Upstream market-data source. Implementations wrap one vendor's HTTP API
// (see AlpacaProvider); everything above this interface is vendor-agnostic,
// so a dead free tier costs one new adapter, not a rewrite.
//
// Symbols arrive pre-validated by construction (src/common/symbol.h).
class MarketDataProvider {
 public:
  virtual ~MarketDataProvider() = default;

  // Real-time last sale, used to execute trades (the anti-front-running rule
  // in docs/ARCHITECTURE.md: execute real-time, chart delayed).
  virtual absl::StatusOr<Trade> GetLatestTrade(const Symbol& symbol) = 0;

  // Daily bars over [start, end], both inclusive, oldest first. Feeds the
  // candles_daily table and the year/YTD/month charts. On the free plan
  // `end` must be a completed day; jobs use the previous trading day.
  virtual absl::StatusOr<std::vector<Bar>> GetDailyBars(
      const Symbol& symbol, absl::CivilDay start, absl::CivilDay end) = 0;

  // Minute bars over [start, end], oldest first, for the intraday chart.
  // Delayed data: on the free plan `end` must be at least 15 minutes in the
  // past; callers clamp.
  virtual absl::StatusOr<std::vector<Bar>> GetMinuteBars(
      const Symbol& symbol, absl::Time start, absl::Time end) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_PROVIDER_H_
