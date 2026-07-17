#ifndef FIREFLY_MARKETDATA_CANDLE_REPO_H_
#define FIREFLY_MARKETDATA_CANDLE_REPO_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "src/db/db.h"
#include "src/marketdata/candle_store.h"
#include "src/common/symbol.h"

namespace firefly {

// Reads and writes over candles_daily, the permanent local store of daily
// bars (docs/ARCHITECTURE.md: fetch once, keep forever; year/YTD/month charts
// are served entirely from here).
class CandleRepo : public CandleStore {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit CandleRepo(Db& db) : db_(db) {}

  // Stored candles over [start, end], both inclusive, oldest first.
  absl::StatusOr<std::vector<DailyCandle>> GetRange(const Symbol& symbol,
                                                    absl::CivilDay start,
                                                    absl::CivilDay end) override;

  // Inserts `candles` for `symbol` in one statement; days already stored are
  // left untouched (history is immutable), so re-runs are idempotent.
  absl::Status UpsertCandles(const Symbol& symbol,
                             const std::vector<DailyCandle>& candles) override;

  // Latest stored day per symbol — the sync job's one-query view of what is
  // already backfilled. Symbols with no candles are absent.
  absl::StatusOr<absl::flat_hash_map<Symbol, absl::CivilDay>> LatestDays()
      override;

 private:
  Db& db_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CANDLE_REPO_H_
