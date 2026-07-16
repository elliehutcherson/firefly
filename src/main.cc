#include <memory>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/api/server.h"
#include "src/auth/crypto.h"
#include "src/auth/session_repo.h"
#include "src/auth/turnstile.h"
#include "src/auth/user_repo.h"
#include "src/common/clock.h"
#include "src/common/config.h"
#include "src/common/db.h"
#include "src/common/http.h"
#include "src/jobs/daily_bar_sync.h"
#include "src/jobs/job_runner.h"
#include "src/marketdata/alpaca.h"
#include "src/marketdata/cached_provider.h"
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

  if (const absl::Status crypto = firefly::InitCrypto(); !crypto.ok()) {
    LOG(ERROR) << "libsodium initialization failed: " << crypto;
    return 1;
  }

  firefly::Config config = firefly::Config::FromEnv();

  absl::StatusOr<std::unique_ptr<firefly::Db>> db =
      firefly::OpenDb(config.database_url);
  if (!db.ok()) {
    LOG(ERROR) << "failed to open database: " << db.status();
    LOG(ERROR) << "is it running? try: docker compose up -d db";
    return 1;
  }

  // Plumbing; declaration order is teardown order in reverse, so the job
  // runners (destroyed first) may use everything above them.
  const std::unique_ptr<firefly::Clock> clock = firefly::CreateSystemClock();
  const std::unique_ptr<firefly::HttpClient> http = firefly::CreateHttpClient();
  firefly::InstrumentRepo instruments(db->get());
  firefly::CandleRepo candles(db->get());
  firefly::UserRepo users(db->get());
  firefly::SessionRepo sessions(db->get());

  firefly::TurnstileVerifier turnstile(config.turnstile_secret_key, http.get());
  if (!turnstile.enabled()) {
    LOG(WARNING) << "TURNSTILE_SECRET_KEY not set; signup/login skip human "
                    "verification";
  }
  firefly::JobRunner session_purge("session_purge", absl::Hours(1), [&] {
    const absl::StatusOr<int64_t> deleted =
        sessions.DeleteExpiredSessions(clock->Now());
    if (!deleted.ok()) {
      LOG(ERROR) << "session purge: " << deleted.status();
      return;
    }
    LOG(INFO) << "session purge: " << *deleted << " expired sessions deleted";
  });
  session_purge.Start();

  std::unique_ptr<firefly::AlpacaProvider> alpaca;
  std::unique_ptr<firefly::CachedProvider> provider;
  std::unique_ptr<firefly::JobRunner> bar_sync;
  if (!config.alpaca_key_id.empty() && !config.alpaca_secret_key.empty()) {
    alpaca = std::make_unique<firefly::AlpacaProvider>(
        firefly::AlpacaConfig{.key_id = config.alpaca_key_id,
                              .secret_key = config.alpaca_secret_key},
        http.get());
    // Request-time callers go through the cache; the sync job's daily bars
    // pass through it unchanged.
    provider = std::make_unique<firefly::CachedProvider>(
        alpaca.get(), clock.get(), firefly::CacheOptions{});
    bar_sync = std::make_unique<firefly::JobRunner>(
        "daily_bar_sync", absl::Hours(1), [&] {
          firefly::DailyBarSyncOptions options;
          options.backfill_start =
              absl::ToCivilDay(clock->Now(), firefly::NewYorkTimeZone()) -
              kJobLookbackDays;
          const absl::StatusOr<firefly::DailyBarSyncStats> stats =
              firefly::SyncDailyBars(instruments, candles, *provider, *clock,
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
  firefly::Server server(
      config,
      {.db = db->get(),
       .market = {.instruments = &instruments,
                  .candles = &candles,
                  .provider = provider.get(),
                  .clock = clock.get()},
       .auth = {.users = &users,
                .sessions = &sessions,
                .turnstile = &turnstile,
                .clock = clock.get(),
                .session_ttl = absl::Hours(24 * config.session_ttl_days),
                .signup_ip_daily_cap = config.signup_ip_daily_cap}});
  server.Run();
  return 0;
}
