#ifndef FIREFLY_MARKETDATA_CANDLE_REPO_H_
#define FIREFLY_MARKETDATA_CANDLE_REPO_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "src/common/db.h"

namespace firefly {

// One persisted daily candle. Keyed by the New York trading date, not an
// instant — daily data is calendar data, and a CivilDay avoids timezone bugs
// at the database and JSON boundaries. Prices are e4 fixed point.
struct DailyCandle {
  absl::CivilDay day;
  int64_t open_e4 = 0;
  int64_t high_e4 = 0;
  int64_t low_e4 = 0;
  int64_t close_e4 = 0;
  int64_t volume = 0;
};

// Reads and writes over candles_daily, the permanent local store of daily
// bars (docs/ARCHITECTURE.md: fetch once, keep forever; year/YTD/month charts
// are served entirely from here).
class CandleRepo {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit CandleRepo(Db* db) : db_(db) {}

  // Stored candles over [start, end], both inclusive, oldest first.
  absl::StatusOr<std::vector<DailyCandle>> GetRange(const std::string& symbol,
                                                    absl::CivilDay start,
                                                    absl::CivilDay end);

  // Inserts `candles` for `symbol` in one statement; days already stored are
  // left untouched (history is immutable), so re-runs are idempotent.
  absl::Status UpsertCandles(const std::string& symbol,
                             const std::vector<DailyCandle>& candles);

  // Latest stored day per symbol — the sync job's one-query view of what is
  // already backfilled. Symbols with no candles are absent.
  absl::StatusOr<absl::flat_hash_map<std::string, absl::CivilDay>> LatestDays();

 private:
  Db* const db_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CANDLE_REPO_H_
