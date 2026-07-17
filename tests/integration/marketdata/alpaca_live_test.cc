#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/common/config.h"
#include "src/common/http.h"
#include "src/common/money.h"
#include "src/marketdata/alpaca.h"
#include "src/marketdata/provider.h"
#include "tests/support/status_matchers.h"
#include "tests/support/symbol.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

// Live integration tests against the real Alpaca data API; the one place the
// vendor boundary is checked. Skipped unless APCA_API_KEY_ID /
// APCA_API_SECRET_KEY are set (a free paper-trading account works).
// Run with: ctest --test-dir build -L integration
class AlpacaLiveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const Config config = Config::FromEnv();
    if (config.alpaca_key_id.empty() || config.alpaca_secret_key.empty()) {
      GTEST_SKIP()
          << "set APCA_API_KEY_ID / APCA_API_SECRET_KEY to run live Alpaca "
             "integration tests";
    }
    http_ = CreateHttpClient();
    provider_ = std::make_unique<AlpacaProvider>(
        AlpacaConfig{.key_id = config.alpaca_key_id,
                     .secret_key = config.alpaca_secret_key},
        *http_);
  }

  std::unique_ptr<HttpClient> http_;
  std::unique_ptr<AlpacaProvider> provider_;
};

TEST_F(AlpacaLiveTest, LatestTradeForAaplIsRecentAndPositive) {
  absl::StatusOr<Trade> trade = provider_->GetLatestTrade(Sym("AAPL"));
  ASSERT_OK(trade);
  EXPECT_GT(trade->price_e4, 0);
  // A week absorbs long weekends and market holidays on the IEX feed.
  EXPECT_GT(trade->time, absl::Now() - absl::Hours(24 * 7));
  EXPECT_LT(trade->time, absl::Now() + absl::Minutes(5));
}

TEST_F(AlpacaLiveTest, HistoricDailyBarsAreStable) {
  // A clean Mon-Fri trading week with no US market holiday. Exact prices
  // are not asserted: adjustment=split rescales history at every split.
  absl::StatusOr<std::vector<Bar>> bars = provider_->GetDailyBars(
      Sym("AAPL"), absl::CivilDay(2024, 1, 8), absl::CivilDay(2024, 1, 12));
  ASSERT_OK(bars);
  ASSERT_EQ(bars->size(), 5);

  absl::TimeZone eastern;
  ASSERT_TRUE(absl::LoadTimeZone("America/New_York", &eastern));
  for (int i = 0; i < 5; ++i) {
    const Bar& bar = (*bars)[i];
    EXPECT_EQ(absl::ToCivilDay(bar.time, eastern), absl::CivilDay(2024, 1, 8 + i));
    EXPECT_LE(bar.low_e4, bar.open_e4);
    EXPECT_LE(bar.low_e4, bar.close_e4);
    EXPECT_GE(bar.high_e4, bar.open_e4);
    EXPECT_GE(bar.high_e4, bar.close_e4);
    EXPECT_GT(bar.volume, 0);
    EXPECT_GT(bar.close_e4, 10 * kPriceScale);
    EXPECT_LT(bar.close_e4, 1000 * kPriceScale);
  }
}

TEST_F(AlpacaLiveTest, UnknownSymbolIsNotFound) {
  // Vendor-behavior-dependent: pins Alpaca's 404 for unknown symbols, the
  // one live check that the StatusFromHttp mapping matches reality.
  EXPECT_THAT(provider_->GetLatestTrade(Sym("ZZZZZZZZ")),
              StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace firefly
