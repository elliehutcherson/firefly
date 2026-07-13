#include "src/marketdata/alpaca.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "nlohmann/json.hpp"
#include "src/common/http.h"
#include "src/common/money.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

using ::nlohmann::json;

constexpr int kMaxSymbolLength = 10;
// Alpaca's maximum; a year of daily bars or a day of minute bars fits in one
// page, so pagination only matters for multi-year backfills.
constexpr char kBarLimit[] = "10000";
constexpr int kMaxBarPages = 100;
constexpr int kMaxErrorBodyLength = 200;

constexpr int kHttpOk = 200;
constexpr int kHttpUnauthorized = 401;
constexpr int kHttpForbidden = 403;
constexpr int kHttpNotFound = 404;
constexpr int kHttpUnprocessable = 422;
constexpr int kHttpTooManyRequests = 429;
constexpr int kHttpServerError = 500;

// Uppercase letters plus '.' and '-' (BRK.B). Callers normalize case.
bool IsValidSymbol(const std::string& symbol) {
  if (symbol.empty() || symbol.size() > kMaxSymbolLength) {
    return false;
  }
  for (const char c : symbol) {
    if (!absl::ascii_isupper(c) && c != '.' && c != '-') {
      return false;
    }
  }
  return true;
}

std::string FormatRfc3339(absl::Time time) {
  return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", time, absl::UTCTimeZone());
}

absl::Status StatusFromHttp(const HttpResponse& response) {
  const std::string message =
      absl::StrCat("alpaca returned HTTP ", response.status_code, ": ",
                   response.body.substr(0, kMaxErrorBodyLength));
  if (response.status_code == kHttpUnauthorized ||
      response.status_code == kHttpForbidden) {
    return absl::PermissionDeniedError(message);
  }
  if (response.status_code == kHttpNotFound) {
    return absl::NotFoundError(message);
  }
  if (response.status_code == kHttpUnprocessable) {
    return absl::InvalidArgumentError(message);
  }
  if (response.status_code == kHttpTooManyRequests) {
    return absl::ResourceExhaustedError(message);
  }
  if (response.status_code >= kHttpServerError) {
    return absl::UnavailableError(message);
  }
  return absl::InternalError(message);
}

// Authenticated GET returning parsed JSON; non-200 becomes an error Status.
absl::StatusOr<json> GetJson(
    HttpClient& http, const AlpacaConfig& config, const std::string& path,
    std::vector<std::pair<std::string, std::string>> query_params) {
  HttpRequest request;
  request.url = absl::StrCat(config.base_url, path);
  request.headers = {{"APCA-API-KEY-ID", config.key_id},
                     {"APCA-API-SECRET-KEY", config.secret_key}};
  request.query_params = std::move(query_params);
  ASSIGN_OR_RETURN(HttpResponse response, http.Get(request));
  if (response.status_code != kHttpOk) {
    return StatusFromHttp(response);
  }
  json parsed = json::parse(response.body, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    return absl::InternalError(
        absl::StrCat("alpaca sent unparseable JSON: ",
                     response.body.substr(0, kMaxErrorBodyLength)));
  }
  return parsed;
}

absl::StatusOr<const json*> GetField(const json& object,
                                     const std::string& key) {
  if (!object.is_object()) {
    return absl::InternalError(
        absl::StrCat("alpaca response is not an object around '", key, "'"));
  }
  const auto found = object.find(key);
  if (found == object.end()) {
    return absl::InternalError(
        absl::StrCat("alpaca response is missing '", key, "'"));
  }
  return &*found;
}

absl::StatusOr<absl::Time> ParseTimestamp(const json& value) {
  if (!value.is_string()) {
    return absl::InternalError("alpaca timestamp is not a string");
  }
  absl::Time time;
  std::string error;
  if (!absl::ParseTime(absl::RFC3339_full, value.get<std::string>(), &time,
                       &error)) {
    return absl::InternalError(
        absl::StrCat("alpaca sent a bad timestamp: ", error));
  }
  return time;
}

absl::StatusOr<int64_t> PriceField(const json& object, const std::string& key) {
  ASSIGN_OR_RETURN(const json* value, GetField(object, key));
  if (!value->is_number()) {
    return absl::InternalError(
        absl::StrCat("alpaca field '", key, "' is not a number"));
  }
  return PriceE4FromDouble(value->get<double>());
}

absl::StatusOr<Bar> ParseBar(const json& bar_json) {
  Bar bar;
  ASSIGN_OR_RETURN(const json* time_json, GetField(bar_json, "t"));
  ASSIGN_OR_RETURN(bar.time, ParseTimestamp(*time_json));
  ASSIGN_OR_RETURN(bar.open_e4, PriceField(bar_json, "o"));
  ASSIGN_OR_RETURN(bar.high_e4, PriceField(bar_json, "h"));
  ASSIGN_OR_RETURN(bar.low_e4, PriceField(bar_json, "l"));
  ASSIGN_OR_RETURN(bar.close_e4, PriceField(bar_json, "c"));
  ASSIGN_OR_RETURN(const json* volume_json, GetField(bar_json, "v"));
  if (!volume_json->is_number()) {
    return absl::InternalError("alpaca bar volume is not a number");
  }
  bar.volume = volume_json->get<int64_t>();
  return bar;
}

}  // namespace

AlpacaProvider::AlpacaProvider(AlpacaConfig config, HttpClient* http)
    : config_(std::move(config)), http_(http) {}

absl::StatusOr<Trade> AlpacaProvider::GetLatestTrade(
    const std::string& symbol) {
  if (!IsValidSymbol(symbol)) {
    return absl::InvalidArgumentError(absl::StrCat("bad symbol: ", symbol));
  }
  ASSIGN_OR_RETURN(
      json body, GetJson(*http_, config_,
                         absl::StrCat("/v2/stocks/", symbol, "/trades/latest"),
                         {{"feed", "iex"}}));
  ASSIGN_OR_RETURN(const json* trade_json, GetField(body, "trade"));
  Trade trade;
  ASSIGN_OR_RETURN(trade.price_e4, PriceField(*trade_json, "p"));
  ASSIGN_OR_RETURN(const json* time_json, GetField(*trade_json, "t"));
  ASSIGN_OR_RETURN(trade.time, ParseTimestamp(*time_json));
  return trade;
}

absl::StatusOr<std::vector<Bar>> AlpacaProvider::GetDailyBars(
    const std::string& symbol, absl::CivilDay start, absl::CivilDay end) {
  return FetchBars(symbol, "1Day", absl::FormatCivilTime(start),
                   absl::FormatCivilTime(end), "split");
}

absl::StatusOr<std::vector<Bar>> AlpacaProvider::GetMinuteBars(
    const std::string& symbol, absl::Time start, absl::Time end) {
  return FetchBars(symbol, "1Min", FormatRfc3339(start), FormatRfc3339(end),
                   "raw");
}

absl::StatusOr<std::vector<Bar>> AlpacaProvider::FetchBars(
    const std::string& symbol, const std::string& timeframe,
    const std::string& start, const std::string& end,
    const std::string& adjustment) {
  if (!IsValidSymbol(symbol)) {
    return absl::InvalidArgumentError(absl::StrCat("bad symbol: ", symbol));
  }
  const std::string path = absl::StrCat("/v2/stocks/", symbol, "/bars");
  std::vector<Bar> bars;
  std::string page_token;
  for (int page = 0; page < kMaxBarPages; ++page) {
    std::vector<std::pair<std::string, std::string>> query_params = {
        {"timeframe", timeframe},   {"start", start}, {"end", end},
        {"adjustment", adjustment}, {"feed", "sip"},  {"limit", kBarLimit}};
    if (!page_token.empty()) {
      query_params.emplace_back("page_token", page_token);
    }
    ASSIGN_OR_RETURN(json body,
                     GetJson(*http_, config_, path, std::move(query_params)));
    // "bars" is null (not an empty array) when the range has no data.
    const auto bars_json = body.find("bars");
    if (bars_json != body.end() && bars_json->is_array()) {
      for (const json& bar_json : *bars_json) {
        ASSIGN_OR_RETURN(Bar bar, ParseBar(bar_json));
        bars.push_back(bar);
      }
    }
    const auto token_json = body.find("next_page_token");
    if (token_json == body.end() || !token_json->is_string()) {
      return bars;
    }
    page_token = token_json->get<std::string>();
  }
  return absl::InternalError("alpaca pagination did not terminate");
}

}  // namespace firefly
