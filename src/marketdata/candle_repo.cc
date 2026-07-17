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
#include "src/common/money.h"
#include "src/common/status_macros.h"
#include "src/db/db.h"
#include "src/db/row_reader.h"

namespace firefly {
namespace {

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
    const Symbol& symbol, absl::CivilDay start, absl::CivilDay end) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT day, open, high, low, close, volume "
                 "FROM candles_daily "
                 "WHERE symbol = $1 AND day >= $2 AND day <= $3 ORDER BY day",
                 {symbol.str(), absl::FormatCivilTime(start),
                  absl::FormatCivilTime(end)}));
  std::vector<DailyCandle> candles;
  candles.reserve(rows.size());
  for (const Row& row : rows) {
    const RowReader reader(row, "candles_daily");
    DailyCandle candle;
    ASSIGN_OR_RETURN(candle.day, reader.CivilDay(0));
    ASSIGN_OR_RETURN(const absl::string_view open, reader.RequiredString(1));
    ASSIGN_OR_RETURN(candle.open_e4, PriceE4FromString(open));
    ASSIGN_OR_RETURN(const absl::string_view high, reader.RequiredString(2));
    ASSIGN_OR_RETURN(candle.high_e4, PriceE4FromString(high));
    ASSIGN_OR_RETURN(const absl::string_view low, reader.RequiredString(3));
    ASSIGN_OR_RETURN(candle.low_e4, PriceE4FromString(low));
    ASSIGN_OR_RETURN(const absl::string_view close, reader.RequiredString(4));
    ASSIGN_OR_RETURN(candle.close_e4, PriceE4FromString(close));
    ASSIGN_OR_RETURN(candle.volume, reader.Int64(5));
    candles.push_back(candle);
  }
  return candles;
}

absl::Status CandleRepo::UpsertCandles(const Symbol& symbol,
                                       const std::vector<DailyCandle>& candles) {
  if (candles.empty()) return absl::OkStatus();
  return db_.Execute(
             "INSERT INTO candles_daily "
             "(symbol, day, open, high, low, close, volume) "
             "SELECT $1::text, * FROM unnest("
             "$2::date[], $3::numeric[], $4::numeric[], $5::numeric[], "
             "$6::numeric[], $7::bigint[]) "
             "ON CONFLICT (symbol, day) DO NOTHING",
             {symbol.str(),
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
              ArrayLiteral(candles, [](const DailyCandle& c) {
                return absl::StrCat(c.volume);
              })})
      .status();
}

absl::StatusOr<absl::flat_hash_map<Symbol, absl::CivilDay>>
CandleRepo::LatestDays() {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT symbol, max(day) FROM candles_daily GROUP BY symbol"));
  absl::flat_hash_map<Symbol, absl::CivilDay> latest;
  latest.reserve(rows.size());
  for (const Row& row : rows) {
    const RowReader reader(row, "candles_daily");
    ASSIGN_OR_RETURN(const absl::string_view raw, reader.RequiredString(0));
    ASSIGN_OR_RETURN(const absl::CivilDay day, reader.CivilDay(1));
    // The 0003 regex CHECK guarantees stored symbols parse; a failure here
    // is a corrupted row, and Parse's InvalidArgument surfaces it.
    ASSIGN_OR_RETURN(const Symbol symbol, Symbol::Parse(raw));
    latest[symbol] = day;
  }
  return latest;
}

}  // namespace firefly
