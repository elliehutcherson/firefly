#ifndef FIREFLY_MARKETDATA_CANDLE_STORE_H_
#define FIREFLY_MARKETDATA_CANDLE_STORE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "src/common/symbol.h"

namespace firefly {

// One persisted daily candle. Keyed by the New York trading date, not an
// instant, to avoid timezone ambiguity at database and JSON boundaries.
// Prices are e4 fixed point.
struct DailyCandle {
  absl::CivilDay day;
  int64_t open_e4 = 0;
  int64_t high_e4 = 0;
  int64_t low_e4 = 0;
  int64_t close_e4 = 0;
  int64_t volume = 0;
};

// Market data's persistence boundary for immutable daily candles.
class CandleStore {
 public:
  virtual ~CandleStore() = default;

  virtual absl::StatusOr<std::vector<DailyCandle>> GetRange(
      const Symbol& symbol, absl::CivilDay start,
      absl::CivilDay end) = 0;
  virtual absl::Status UpsertCandles(
      const Symbol& symbol,
      const std::vector<DailyCandle>& candles) = 0;
  virtual absl::StatusOr<
      absl::flat_hash_map<Symbol, absl::CivilDay>>
  LatestDays() = 0;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CANDLE_STORE_H_
