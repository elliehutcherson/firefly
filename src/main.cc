#include <memory>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/api/server.h"
#include "src/common/clock.h"
#include "src/common/config.h"
#include "src/common/db.h"
#include "src/common/http.h"
#include "src/jobs/daily_bar_sync.h"
#include "src/jobs/job_runner.h"
#include "src/marketdata/alpaca.h"
#include "src/marketdata/candle_repo.h"
#include "src/marketdata/instrument_repo.h"

namespace {

// The hourly self-healing lookback: far enough to catch up after a weekend
// of downtime, never a silent multi-year pull (that's backfill_bars).
constexpr int kJobLookbackDays = 7;

}  // namespace

int main() {
  absl::InitializeLog();
  // stderr is the only log destination (journald/docker capture it); the
  // default ERROR threshold would hide startup lines and job stats.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  firefly::Config config = firefly::Config::FromEnv();

  absl::StatusOr<std::unique_ptr<firefly::Db>> db =
      firefly::OpenDb(config.database_url);
  if (!db.ok()) {
    LOG(ERROR) << "failed to open database: " << db.status();
    LOG(ERROR) << "is it running? try: docker compose up -d db";
    return 1;
  }

  // Market data plumbing; declaration order is teardown order in reverse, so
  // the job runner (destroyed first) may use everything above it.
  const std::unique_ptr<firefly::Clock> clock = firefly::CreateSystemClock();
  std::unique_ptr<firefly::HttpClient> http;
  std::unique_ptr<firefly::AlpacaProvider> provider;
  std::unique_ptr<firefly::InstrumentRepo> instruments;
  std::unique_ptr<firefly::CandleRepo> candles;
  std::unique_ptr<firefly::JobRunner> bar_sync;
  if (!config.alpaca_key_id.empty() && !config.alpaca_secret_key.empty()) {
    http = firefly::CreateHttpClient();
    provider = std::make_unique<firefly::AlpacaProvider>(
        firefly::AlpacaConfig{.key_id = config.alpaca_key_id,
                              .secret_key = config.alpaca_secret_key},
        http.get());
    instruments = std::make_unique<firefly::InstrumentRepo>(db->get());
    candles = std::make_unique<firefly::CandleRepo>(db->get());
    bar_sync = std::make_unique<firefly::JobRunner>(
        "daily_bar_sync", absl::Hours(1), [&] {
          firefly::DailyBarSyncOptions options;
          options.backfill_start =
              absl::ToCivilDay(clock->Now(), firefly::NewYorkTimeZone()) -
              kJobLookbackDays;
          const absl::StatusOr<firefly::DailyBarSyncStats> stats =
              firefly::SyncDailyBars(*instruments, *candles, *provider, *clock,
                                     options);
          if (!stats.ok()) {
            LOG(ERROR) << "daily bar sync: " << stats.status();
            return;
          }
          LOG(INFO) << "daily bar sync: " << stats->symbols_fetched << "/"
                    << stats->symbols_checked << " symbols fetched, "
                    << stats->candles_written << " candles written, "
                    << stats->failures << " failures";
        });
    bar_sync->Start();
  } else {
    LOG(WARNING) << "APCA_API_KEY_ID / APCA_API_SECRET_KEY not set; "
                    "market data and the bar sync job are disabled";
  }

  LOG(INFO) << "firefly listening on " << config.bind_address << ":"
            << config.port;
  firefly::Server server(config, db->get());
  server.Run();
  return 0;
}
