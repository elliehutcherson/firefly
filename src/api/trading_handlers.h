#ifndef FIREFLY_API_TRADING_HANDLERS_H_
#define FIREFLY_API_TRADING_HANDLERS_H_

#include <string>

#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"
#include "auth/session_auth.h"
#include "common/clock.h"
#include "marketdata/instrument_store.h"
#include "marketdata/provider.h"
#include "trading/trading_store.h"

namespace firefly {

// Trading endpoint cores, crow-free like the other handler files: strings
// in (cookie token, request body JSON), JSON out. server.cc owns HTTP
// framing; both endpoints are no-store.
//
// Orders execute at the real-time IEX price fetched HERE, before
// TradingStore's transaction, so network I/O never runs under the users-row
// lock (and the anti-cheat rule holds: execute real-time, chart delayed).

// All borrowed, all non-owning. `provider` may be nullptr (no Alpaca
// credentials): orders return Unavailable. `allow_closed_market` is the
// FIREFLY_ALLOW_CLOSED_MARKET_TRADING dev bypass — without it local dev
// outside 9:30-16:00 ET could never trade; never enable it in production.
struct TradingDeps {
  TradingStore& trading;
  InstrumentStore& instruments;
  MarketDataProvider* provider = nullptr;
  SessionAuth session_auth;
  const Clock& clock;
  bool allow_closed_market = false;
};

// Body: {"symbol","side","quantity"} — side is buy/sell/short/cover,
// quantity a JSON integer in [1, kMaxOrderQuantity]. Checks run in order:
// session (401) -> shape (400) -> unknown symbol (404) -> market closed
// (422) -> provider missing (503) -> quote -> ExecuteOrder (state
// rejections 422). Response: {"order_id","symbol","side","quantity",
// "price","cash_delta_cents","cash_cents","position"}, prices as 4-decimal
// strings, position null when the order left the account flat.
absl::StatusOr<nlohmann::json> PlaceOrderJson(const TradingDeps& deps,
                                              const std::string& cookie_token,
                                              const std::string& body_json);

// {"cash_cents","positions":[{"symbol","quantity","avg_price"}...]} in one
// consistent snapshot; negative quantity = short.
absl::StatusOr<nlohmann::json> GetPortfolioJson(
    const TradingDeps& deps, const std::string& cookie_token);

}  // namespace firefly

#endif  // FIREFLY_API_TRADING_HANDLERS_H_
