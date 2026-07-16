#include "src/api/market_handlers.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "nlohmann/json.hpp"
#include "src/common/clock.h"
#include "src/common/money.h"
#include "src/common/status_macros.h"
#include "src/marketdata/provider.h"

namespace firefly {
namespace {

constexpr size_t kMaxSymbolLength = 10;

// The free plan rejects ranges touching the last 15 minutes; one extra
// minute of margin avoids racing the boundary.
constexpr absl::Duration kIntradayDelay = absl::Minutes(16);

std::string FormatUtc(absl::Time time) {
  // RFC3339 with the literal Z suffix (absl's RFC3339_sec emits "+00:00").
  return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", time, absl::UTCTimeZone());
}

// Symbol must already be normalized. NotFound for anything outside the
// curated universe.
absl::Status RequireKnownSymbol(const MarketDeps& deps,
                                const std::string& symbol) {
  ASSIGN_OR_RETURN(const bool exists, deps.instruments->Exists(symbol));
  if (!exists) {
    return absl::NotFoundError(absl::StrCat("unknown symbol: ", symbol));
  }
  return absl::OkStatus();
}

absl::Status RequireProvider(const MarketDeps& deps) {
  if (deps.provider == nullptr) {
    return absl::UnavailableError("market data not configured");
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::CivilDay> RangeStart(const std::string& range,
                                          absl::CivilDay today) {
  // CivilDay normalizes overflow (e.g. March 31 minus one month), which is
  // fine for chart windows.
  if (range == "month") {
    return absl::CivilDay(today.year(), today.month() - 1, today.day());
  }
  if (range == "ytd") {
    return absl::CivilDay(today.year(), 1, 1);
  }
  if (range == "year") {
    return absl::CivilDay(today.year() - 1, today.month(), today.day());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("range must be month, ytd, or year; got: '", range, "'"));
}

nlohmann::json ToJson(const DailyCandle& candle) {
  return {{"day", absl::FormatCivilTime(candle.day)},
          {"open", PriceE4ToString(candle.open_e4)},
          {"high", PriceE4ToString(candle.high_e4)},
          {"low", PriceE4ToString(candle.low_e4)},
          {"close", PriceE4ToString(candle.close_e4)},
          {"volume", candle.volume}};
}

nlohmann::json ToJson(const Bar& bar) {
  return {{"time", FormatUtc(bar.time)},
          {"open", PriceE4ToString(bar.open_e4)},
          {"high", PriceE4ToString(bar.high_e4)},
          {"low", PriceE4ToString(bar.low_e4)},
          {"close", PriceE4ToString(bar.close_e4)},
          {"volume", bar.volume}};
}

// [start, end] of the intraday window: today's session clamped to the
// delayed feed, or the previous day's full session before the market opens.
// The end is floored to the whole minute so all requests within one minute
// produce the same CachedProvider key.
struct IntradayWindow {
  absl::Time start;
  absl::Time end;
};

IntradayWindow ComputeIntradayWindow(absl::Time now, absl::TimeZone new_york) {
  absl::CivilDay day = absl::ToCivilDay(now, new_york);
  absl::Time open =
      absl::FromCivil(absl::CivilMinute(day.year(), day.month(), day.day(),
                                        9, 30),
                      new_york);
  const absl::Time delayed = absl::FromCivil(
      absl::ToCivilMinute(now - kIntradayDelay, new_york), new_york);
  if (delayed <= open) {  // Before/near the open: show the last session.
    day -= 1;
    open = absl::FromCivil(absl::CivilMinute(day.year(), day.month(),
                                             day.day(), 9, 30),
                           new_york);
  }
  const absl::Time close = absl::FromCivil(
      absl::CivilMinute(day.year(), day.month(), day.day(), 16, 0), new_york);
  return {.start = open, .end = std::min(delayed, close)};
}

}  // namespace

absl::StatusOr<std::string> NormalizeSymbol(const std::string& raw) {
  if (raw.empty() || raw.size() > kMaxSymbolLength) {
    return absl::InvalidArgumentError("invalid symbol");
  }
  std::string symbol = raw;
  std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  });
  const auto is_valid = [&](char c, bool first) {
    return (c >= 'A' && c <= 'Z') || (!first && (c == '.' || c == '-'));
  };
  for (size_t i = 0; i < symbol.size(); ++i) {
    if (!is_valid(symbol[i], i == 0)) {
      return absl::InvalidArgumentError("invalid symbol");
    }
  }
  return symbol;
}

absl::StatusOr<nlohmann::json> GetQuoteJson(const MarketDeps& deps,
                                            const std::string& raw_symbol) {
  ASSIGN_OR_RETURN(const std::string symbol, NormalizeSymbol(raw_symbol));
  RETURN_IF_ERROR(RequireKnownSymbol(deps, symbol));
  RETURN_IF_ERROR(RequireProvider(deps));
  ASSIGN_OR_RETURN(const Trade trade, deps.provider->GetLatestTrade(symbol));
  return nlohmann::json{{"symbol", symbol},
                        {"price", PriceE4ToString(trade.price_e4)},
                        {"time", FormatUtc(trade.time)}};
}

absl::StatusOr<nlohmann::json> GetDailyCandlesJson(
    const MarketDeps& deps, const std::string& raw_symbol,
    const std::string& range) {
  ASSIGN_OR_RETURN(const std::string symbol, NormalizeSymbol(raw_symbol));
  RETURN_IF_ERROR(RequireKnownSymbol(deps, symbol));
  const absl::CivilDay today =
      absl::ToCivilDay(deps.clock->Now(), NewYorkTimeZone());
  ASSIGN_OR_RETURN(const absl::CivilDay start, RangeStart(range, today));
  ASSIGN_OR_RETURN(const std::vector<DailyCandle> candles,
                   deps.candles->GetRange(symbol, start, today));
  nlohmann::json bars = nlohmann::json::array();
  for (const DailyCandle& candle : candles) {
    bars.push_back(ToJson(candle));
  }
  return nlohmann::json{
      {"symbol", symbol}, {"range", range}, {"bars", std::move(bars)}};
}

absl::StatusOr<nlohmann::json> GetIntradayCandlesJson(
    const MarketDeps& deps, const std::string& raw_symbol) {
  ASSIGN_OR_RETURN(const std::string symbol, NormalizeSymbol(raw_symbol));
  RETURN_IF_ERROR(RequireKnownSymbol(deps, symbol));
  RETURN_IF_ERROR(RequireProvider(deps));
  const IntradayWindow window =
      ComputeIntradayWindow(deps.clock->Now(), NewYorkTimeZone());
  ASSIGN_OR_RETURN(
      const std::vector<Bar> minute_bars,
      deps.provider->GetMinuteBars(symbol, window.start, window.end));
  nlohmann::json bars = nlohmann::json::array();
  for (const Bar& bar : minute_bars) {
    bars.push_back(ToJson(bar));
  }
  return nlohmann::json{{"symbol", symbol}, {"bars", std::move(bars)}};
}

int HttpStatusFromCode(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kInvalidArgument:
      return 400;
    case absl::StatusCode::kUnauthenticated:
      return 401;
    case absl::StatusCode::kPermissionDenied:
      return 403;
    case absl::StatusCode::kNotFound:
      return 404;
    case absl::StatusCode::kAlreadyExists:
      return 409;
    case absl::StatusCode::kResourceExhausted:
      return 429;
    case absl::StatusCode::kUnavailable:
      return 503;
    case absl::StatusCode::kDeadlineExceeded:
      return 504;
    default:
      return 500;
  }
}

}  // namespace firefly
