// One-time daily-bar backfill: pulls years of history for the active
// instrument universe into candles_daily, one request per symbol, throttled.
// Idempotent and resumable — rerunning skips days already stored, so a
// crashed or interrupted run just picks up where it left off.
//
//   APCA_API_KEY_ID=... APCA_API_SECRET_KEY=... DATABASE_URL=... \
//     ./build/bin/backfill_bars [--years=7] [--delay_ms=400] \
//     [--symbols=AAPL,MSFT]
//
// STOCK SPLITS: bars are stored split-adjusted as of fetch time. If a symbol
// splits later, its stored history is at the old scale while new nightly
// appends arrive at the new scale — the chart shows a fake cliff. Fix by
// re-backfilling just that symbol:
//
//   psql "$DATABASE_URL" -c "DELETE FROM candles_daily WHERE symbol = 'XYZ'"
//   ./build/bin/backfill_bars --symbols=XYZ

#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/clock.h"
#include "src/common/config.h"
#include "src/common/db.h"
#include "src/common/http.h"
#include "src/jobs/daily_bar_sync.h"
#include "src/marketdata/alpaca.h"
#include "src/marketdata/candle_repo.h"
#include "src/marketdata/instrument_repo.h"

ABSL_FLAG(int, years, 7, "How far back to fetch history");
ABSL_FLAG(int, delay_ms, 400,
          "Pause between symbols; 400ms is ~150 requests/minute");
ABSL_FLAG(std::string, symbols, "",
          "Comma-separated symbols to backfill instead of the full active "
          "universe (e.g. after a stock split; see the file comment)");

namespace {

firefly::DailyBarSyncOptions OptionsFromFlags(const firefly::Clock& clock) {
  firefly::DailyBarSyncOptions options;
  const absl::CivilDay today =
      absl::ToCivilDay(clock.Now(), firefly::NewYorkTimeZone());
  options.backfill_start = absl::CivilDay(
      today.year() - absl::GetFlag(FLAGS_years), today.month(), today.day());
  options.inter_request_delay =
      absl::Milliseconds(absl::GetFlag(FLAGS_delay_ms));
  if (const std::string flag = absl::GetFlag(FLAGS_symbols); !flag.empty()) {
    options.symbols = absl::StrSplit(flag, ',', absl::SkipEmpty());
  }
  return options;
}

int RunSync(firefly::InstrumentRepo& instruments, firefly::CandleRepo& candles,
            firefly::MarketDataProvider& provider,
            const firefly::Clock& clock) {
  const firefly::DailyBarSyncOptions options = OptionsFromFlags(clock);
  LOG(INFO) << "backfilling daily bars from " << options.backfill_start;
  const absl::StatusOr<firefly::DailyBarSyncStats> stats =
      firefly::SyncDailyBars(instruments, candles, provider, clock, options);
  if (!stats.ok()) {
    LOG(ERROR) << "backfill failed: " << stats.status();
    return 1;
  }
  LOG(INFO) << "backfill done: " << stats->symbols_checked << " checked, "
            << stats->symbols_fetched << " fetched, " << stats->candles_written
            << " candles written, " << stats->failures << " failures";
  return stats->failures > 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Backfills daily bars into candles_daily. Idempotent; safe to rerun. "
      "After a stock split, DELETE the symbol's candles and rerun with "
      "--symbols=XYZ (bars are split-adjusted at fetch time).");
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Progress and stats are INFO; the operator watching the backfill needs
  // them on stderr.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  const firefly::Config config = firefly::Config::FromEnv();
  if (config.alpaca_key_id.empty() || config.alpaca_secret_key.empty()) {
    LOG(ERROR) << "set APCA_API_KEY_ID and APCA_API_SECRET_KEY "
                  "(free paper account at https://alpaca.markets)";
    return 1;
  }

  absl::StatusOr<std::unique_ptr<firefly::Db>> db =
      firefly::OpenDb(config.database_url);
  if (!db.ok()) {
    LOG(ERROR) << "failed to open database: " << db.status();
    return 1;
  }

  const std::unique_ptr<firefly::HttpClient> http = firefly::CreateHttpClient();
  firefly::AlpacaProvider provider(
      {.key_id = config.alpaca_key_id, .secret_key = config.alpaca_secret_key},
      http.get());
  const std::unique_ptr<firefly::Clock> clock = firefly::CreateSystemClock();
  firefly::InstrumentRepo instruments(db->get());
  firefly::CandleRepo candles(db->get());

  return RunSync(instruments, candles, provider, *clock);
}
