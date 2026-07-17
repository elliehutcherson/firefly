#ifndef FIREFLY_API_MARKET_HANDLERS_H_
#define FIREFLY_API_MARKET_HANDLERS_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"
#include "common/clock.h"
#include "marketdata/candle_store.h"
#include "marketdata/instrument_store.h"
#include "marketdata/provider.h"

namespace firefly {

// Market-data endpoint cores, kept crow-free so they unit-test with fakes
// (the CheckHealth pattern); server.cc owns the HTTP framing.
//
// Every core validates the symbol against the instruments universe before
// touching anything else — requests for unknown symbols never reach a
// provider or bloat its cache (docs/ARCHITECTURE.md security checklist).
// Prices serialize as 4-decimal strings, never floats
// (backend/src/common/money.h).

// All borrowed, all non-owning. `provider` may be nullptr (no Alpaca
// credentials): quote/intraday return Unavailable, daily still serves from
// Postgres.
struct MarketDeps {
  InstrumentStore& instruments;
  CandleStore& candles;
  MarketDataProvider* provider = nullptr;
  const Clock& clock;
};

// {"symbol","price","time"} — the real-time price a trade would execute at.
absl::StatusOr<nlohmann::json> GetQuoteJson(const MarketDeps& deps,
                                            const std::string& raw_symbol);

// {"symbol","range","bars":[{day,open,high,low,close,volume}...]} served
// entirely from Postgres. `range` is "month", "ytd", or "year".
absl::StatusOr<nlohmann::json> GetDailyCandlesJson(
    const MarketDeps& deps, const std::string& raw_symbol,
    const std::string& range);

// {"symbol","bars":[{time,open,high,low,close,volume}...]} — today's session
// (or the previous one before the market opens), 15-minute-delayed minute
// bars. The range end is floored to the whole minute so concurrent requests
// share one CachedProvider key.
absl::StatusOr<nlohmann::json> GetIntradayCandlesJson(
    const MarketDeps& deps, const std::string& raw_symbol);

}  // namespace firefly

#endif  // FIREFLY_API_MARKET_HANDLERS_H_
