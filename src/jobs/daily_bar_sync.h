#ifndef FIREFLY_JOBS_DAILY_BAR_SYNC_H_
#define FIREFLY_JOBS_DAILY_BAR_SYNC_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/clock.h"
#include "src/marketdata/candle_repo.h"
#include "src/marketdata/instrument_repo.h"
#include "src/marketdata/provider.h"

namespace firefly {

// US market time. Daily bars are keyed by the New York trading date.
absl::TimeZone NewYorkTimeZone();

struct DailyBarSyncOptions {
  // The earliest day worth fetching for a symbol with no stored candles.
  // The backfill CLI passes years ago; the hourly job passes a short
  // lookback so it never silently starts a multi-year pull.
  absl::CivilDay backfill_start;

  // Pause after each provider call: ~150 requests/minute, comfortably under
  // Alpaca's free-tier budget of ~200.
  absl::Duration inter_request_delay = absl::Milliseconds(400);

  // Sync only these symbols instead of the full active universe. Used by the
  // backfill CLI, e.g. to re-backfill one symbol after a stock split.
  std::vector<std::string> symbols;
};

struct DailyBarSyncStats {
  int symbols_checked = 0;
  int symbols_fetched = 0;
  int candles_written = 0;
  int failures = 0;
};

// Brings candles_daily up to date: for every active symbol (or
// options.symbols), fetches daily bars from max(latest stored day + 1,
// backfill_start) through yesterday (New York) and stores them. Symbols
// already current cost zero provider calls, so this is safe to run hourly;
// the same call is the one-time backfill when backfill_start is years back.
// Re-runs are idempotent and resume where they left off.
//
// A failing symbol is logged, counted, and skipped — one bad symbol must not
// starve the rest. Returns non-OK only when nothing succeeded.
absl::StatusOr<DailyBarSyncStats> SyncDailyBars(
    InstrumentRepo& instruments, CandleRepo& candles,
    MarketDataProvider& provider, const Clock& clock,
    const DailyBarSyncOptions& options);

}  // namespace firefly

#endif  // FIREFLY_JOBS_DAILY_BAR_SYNC_H_
