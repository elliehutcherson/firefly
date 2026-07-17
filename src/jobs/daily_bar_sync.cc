#include "src/jobs/daily_bar_sync.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/clock.h"
#include "src/common/status_macros.h"
#include "src/marketdata/candle_store.h"
#include "src/marketdata/instrument_store.h"
#include "src/marketdata/provider.h"

namespace firefly {
namespace {

std::vector<DailyCandle> ToDailyCandles(const std::vector<Bar>& bars,
                                        absl::TimeZone new_york) {
  std::vector<DailyCandle> candles;
  candles.reserve(bars.size());
  for (const Bar& bar : bars) {
    candles.push_back({.day = absl::ToCivilDay(bar.time, new_york),
                       .open_e4 = bar.open_e4,
                       .high_e4 = bar.high_e4,
                       .low_e4 = bar.low_e4,
                       .close_e4 = bar.close_e4,
                       .volume = bar.volume});
  }
  return candles;
}

// Fetches [start, through] for one symbol and stores it. One provider call
// (paginated internally) and at most one insert.
absl::StatusOr<int> SyncSymbol(const Symbol& symbol, absl::CivilDay start,
                               absl::CivilDay through, CandleStore& candles,
                               MarketDataProvider& provider,
                               absl::TimeZone new_york) {
  ASSIGN_OR_RETURN(const std::vector<Bar> bars,
                   provider.GetDailyBars(symbol, start, through));
  // Empty is normal: weekends and holidays have no bars.
  const std::vector<DailyCandle> converted = ToDailyCandles(bars, new_york);
  RETURN_IF_ERROR(candles.UpsertCandles(symbol, converted));
  return static_cast<int>(converted.size());
}

}  // namespace

absl::StatusOr<DailyBarSyncStats> SyncDailyBars(
    InstrumentStore& instruments, CandleStore& candles,
    MarketDataProvider& provider, const Clock& clock,
    const DailyBarSyncOptions& options) {
  const absl::TimeZone new_york = NewYorkTimeZone();
  // Yesterday: the free plan only serves completed days (provider.h).
  const absl::CivilDay through =
      absl::ToCivilDay(clock.Now(), new_york) - 1;

  std::vector<Symbol> symbols = options.symbols;
  if (symbols.empty()) {
    ASSIGN_OR_RETURN(symbols, instruments.ListActiveSymbols());
  }
  ASSIGN_OR_RETURN(const auto latest_days, candles.LatestDays());

  DailyBarSyncStats stats;
  int attempts = 0;
  for (const Symbol& symbol : symbols) {
    ++stats.symbols_checked;

    absl::CivilDay start = options.backfill_start;
    if (const auto it = latest_days.find(symbol); it != latest_days.end()) {
      start = std::max(start, it->second + 1);
    }
    if (start > through) {
      continue;  // Already current: zero provider calls.
    }

    ++attempts;
    const absl::StatusOr<int> written =
        SyncSymbol(symbol, start, through, candles, provider, new_york);
    clock.Sleep(options.inter_request_delay);
    if (!written.ok()) {
      ++stats.failures;
      LOG(WARNING) << "daily bar sync failed for " << symbol << ": "
                   << written.status();
      continue;
    }
    ++stats.symbols_fetched;
    stats.candles_written += *written;
  }

  if (attempts > 0 && stats.symbols_fetched == 0) {
    return absl::UnavailableError(absl::StrCat(
        "daily bar sync: all ", attempts, " fetched symbols failed"));
  }
  return stats;
}

}  // namespace firefly
