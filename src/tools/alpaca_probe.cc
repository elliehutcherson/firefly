// Smoke test for Alpaca credentials: fetches the latest trade and a week of
// daily bars for one symbol, straight through the production provider.
//
//   APCA_API_KEY_ID=... APCA_API_SECRET_KEY=... ./build/bin/alpaca_probe
//   [SYMBOL]

#include <memory>
#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/common/config.h"
#include "src/common/http.h"
#include "src/common/money.h"
#include "src/common/symbol.h"
#include "src/marketdata/alpaca.h"
#include "src/marketdata/provider.h"

namespace {

constexpr int kBackfillDays = 8;

}  // namespace

int main(int argc, char** argv) {
  absl::InitializeLog();
  // The fetched data is the whole point of the tool; it logs at INFO.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  const firefly::Config config = firefly::Config::FromEnv();
  if (config.alpaca_key_id.empty() || config.alpaca_secret_key.empty()) {
    LOG(ERROR) << "set APCA_API_KEY_ID and APCA_API_SECRET_KEY "
                  "(free paper account at https://alpaca.markets)";
    return 1;
  }
  const absl::StatusOr<firefly::Symbol> symbol =
      firefly::Symbol::Parse(argc > 1 ? argv[1] : "AAPL");
  if (!symbol.ok()) {
    LOG(ERROR) << "bad symbol: " << symbol.status();
    return 1;
  }

  const std::unique_ptr<firefly::HttpClient> http = firefly::CreateHttpClient();
  firefly::AlpacaProvider provider(
      {.key_id = config.alpaca_key_id, .secret_key = config.alpaca_secret_key},
      *http);

  const absl::StatusOr<firefly::Trade> trade = provider.GetLatestTrade(*symbol);
  if (!trade.ok()) {
    LOG(ERROR) << "GetLatestTrade(" << *symbol << "): " << trade.status();
    return 1;
  }
  LOG(INFO) << *symbol << " last trade $"
            << firefly::PriceE4ToString(trade->price_e4) << " at "
            << trade->time;

  // End on yesterday: the free plan rejects ranges touching the last 15 min.
  const absl::CivilDay today =
      absl::ToCivilDay(absl::Now(), absl::UTCTimeZone());
  const absl::StatusOr<std::vector<firefly::Bar>> bars =
      provider.GetDailyBars(*symbol, today - kBackfillDays, today - 1);
  if (!bars.ok()) {
    LOG(ERROR) << "GetDailyBars(" << *symbol << "): " << bars.status();
    return 1;
  }
  LOG(INFO) << bars->size() << " daily bars:";
  for (const firefly::Bar& bar : *bars) {
    LOG(INFO) << "  " << absl::ToCivilDay(bar.time, absl::UTCTimeZone())
              << " close $" << firefly::PriceE4ToString(bar.close_e4)
              << " volume " << bar.volume;
  }
  return 0;
}
