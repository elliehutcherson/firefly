#ifndef FIREFLY_MARKETDATA_ALPACA_H_
#define FIREFLY_MARKETDATA_ALPACA_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/http.h"
#include "src/marketdata/provider.h"

namespace firefly {

struct AlpacaConfig {
  // Credentials of a free paper-trading account (alpaca.markets).
  std::string key_id;
  std::string secret_key;
  std::string base_url = "https://data.alpaca.markets";
};

// MarketDataProvider backed by the Alpaca Market Data API (free Basic plan).
// Latest trades come from the real-time IEX feed; bars from the delayed SIP
// feed — see "Anti-cheat pricing rule" in docs/ARCHITECTURE.md. Daily bars
// are split-adjusted so charts don't show cliffs at stock splits.
class AlpacaProvider : public MarketDataProvider {
 public:
  // Pagination safety bound for bar fetches; a multi-year daily backfill
  // fits well within it at Alpaca's 10000-bar page limit.
  static constexpr int kMaxBarPages = 100;

  // `http` is borrowed and must outlive the provider.
  explicit AlpacaProvider(AlpacaConfig config, HttpClient& http);

  absl::StatusOr<Trade> GetLatestTrade(const std::string& symbol) override;
  absl::StatusOr<std::vector<Bar>> GetDailyBars(const std::string& symbol,
                                                absl::CivilDay start,
                                                absl::CivilDay end) override;
  absl::StatusOr<std::vector<Bar>> GetMinuteBars(const std::string& symbol,
                                                 absl::Time start,
                                                 absl::Time end) override;

 private:
  absl::StatusOr<std::vector<Bar>> FetchBars(const std::string& symbol,
                                             const std::string& timeframe,
                                             const std::string& start,
                                             const std::string& end,
                                             const std::string& adjustment);

  const AlpacaConfig config_;
  HttpClient& http_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_ALPACA_H_
