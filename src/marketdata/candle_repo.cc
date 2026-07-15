#include "src/marketdata/candle_repo.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "src/common/db.h"
#include "src/common/money.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

absl::StatusOr<absl::string_view> GetColumn(const Row& row, size_t index) {
  if (index >= row.columns.size() || !row.columns[index].has_value()) {
    return absl::InternalError(
        absl::StrCat("candles_daily row missing column ", index));
  }
  return absl::string_view(*row.columns[index]);
}

absl::StatusOr<absl::CivilDay> ParseDay(absl::string_view text) {
  absl::CivilDay day;
  if (!absl::ParseCivilTime(text, &day)) {
    return absl::InternalError(absl::StrCat("bad date from db: '", text, "'"));
  }
  return day;
}

template <typename T>
concept CandleFieldFormatter =
    std::invocable<T, const DailyCandle&> &&
    std::convertible_to<std::invoke_result_t<T, const DailyCandle&>,
                        std::string>;

// Postgres array literal from already-safe elements (dates and numbers we
// format ourselves — no quoting hazard).
template <CandleFieldFormatter T>
std::string ArrayLiteral(const std::vector<DailyCandle>& candles,
                         T&& element) {
  return absl::StrCat(
      "{",
      absl::StrJoin(candles, ",",
                    [&](std::string* out, const DailyCandle& c) {
                      absl::StrAppend(out, element(c));
                    }),
      "}");
}

}  // namespace

absl::StatusOr<std::vector<DailyCandle>> CandleRepo::GetRange(
    const std::string& symbol, absl::CivilDay start, absl::CivilDay end) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT day, open, high, low, close, volume "
                 "FROM candles_daily "
                 "WHERE symbol = $1 AND day >= $2 AND day <= $3 ORDER BY day",
                 {symbol, absl::FormatCivilTime(start),
                  absl::FormatCivilTime(end)}));
  std::vector<DailyCandle> candles;
  candles.reserve(rows.size());
  for (const Row& row : rows) {
    DailyCandle candle;
    ASSIGN_OR_RETURN(const absl::string_view day, GetColumn(row, 0));
    ASSIGN_OR_RETURN(candle.day, ParseDay(day));
    ASSIGN_OR_RETURN(const absl::string_view open, GetColumn(row, 1));
    ASSIGN_OR_RETURN(candle.open_e4, PriceE4FromString(open));
    ASSIGN_OR_RETURN(const absl::string_view high, GetColumn(row, 2));
    ASSIGN_OR_RETURN(candle.high_e4, PriceE4FromString(high));
    ASSIGN_OR_RETURN(const absl::string_view low, GetColumn(row, 3));
    ASSIGN_OR_RETURN(candle.low_e4, PriceE4FromString(low));
    ASSIGN_OR_RETURN(const absl::string_view close, GetColumn(row, 4));
    ASSIGN_OR_RETURN(candle.close_e4, PriceE4FromString(close));
    ASSIGN_OR_RETURN(const absl::string_view volume, GetColumn(row, 5));
    if (!absl::SimpleAtoi(volume, &candle.volume)) {
      return absl::InternalError(
          absl::StrCat("bad volume from db: '", volume, "'"));
    }
    candles.push_back(candle);
  }
  return candles;
}

absl::Status CandleRepo::UpsertCandles(const std::string& symbol,
                                       const std::vector<DailyCandle>& candles) {
  if (candles.empty()) return absl::OkStatus();
  return db_
      ->Execute(
          "INSERT INTO candles_daily "
          "(symbol, day, open, high, low, close, volume) "
          "SELECT $1::text, * FROM unnest("
          "$2::date[], $3::numeric[], $4::numeric[], $5::numeric[], "
          "$6::numeric[], $7::bigint[]) "
          "ON CONFLICT (symbol, day) DO NOTHING",
          {symbol,
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return absl::FormatCivilTime(c.day);
                        }),
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return PriceE4ToString(c.open_e4);
                        }),
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return PriceE4ToString(c.high_e4);
                        }),
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return PriceE4ToString(c.low_e4);
                        }),
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return PriceE4ToString(c.close_e4);
                        }),
           ArrayLiteral(candles,
                        [](const DailyCandle& c) {
                          return absl::StrCat(c.volume);
                        })})
      .status();
}

absl::StatusOr<absl::flat_hash_map<std::string, absl::CivilDay>>
CandleRepo::LatestDays() {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT symbol, max(day) FROM candles_daily GROUP BY symbol"));
  absl::flat_hash_map<std::string, absl::CivilDay> latest;
  latest.reserve(rows.size());
  for (const Row& row : rows) {
    ASSIGN_OR_RETURN(const absl::string_view symbol, GetColumn(row, 0));
    ASSIGN_OR_RETURN(const absl::string_view day, GetColumn(row, 1));
    ASSIGN_OR_RETURN(latest[symbol], ParseDay(day));
  }
  return latest;
}

}  // namespace firefly
