#ifndef FIREFLY_TRADING_MARKET_CALENDAR_H_
#define FIREFLY_TRADING_MARKET_CALENDAR_H_

#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace firefly {

// Computed NYSE calendar: the ten rule-based full-day holidays (New Year's
// Day, MLK Day, Washington's Birthday, Good Friday, Memorial Day, Juneteenth,
// Independence Day, Labor Day, Thanksgiving, Christmas), with fixed-date
// holidays observed on the nearest weekday (Saturday -> Friday,
// Sunday -> Monday). No maintained list: every date derives from the rules,
// including Good Friday via the Gregorian Easter algorithm.
//
// Documented v1 deviations from the real NYSE calendar:
//   * No early closes (day after Thanksgiving, Christmas Eve) — the market is
//     treated as fully open on those days.
//   * Saturday observance is applied across the year boundary: when New
//     Year's Day falls on a Saturday we close the preceding Friday
//     (Dec 31), while the real NYSE stays open that day. Next divergence:
//     2027-12-31.

// True when the NYSE is closed for a holiday on `day` (New York dates).
bool IsNyseHoliday(absl::CivilDay day);

// True when `day` is a weekday and not a holiday.
bool IsTradingDay(absl::CivilDay day);

// True when `time` falls within a regular session: [09:30, 16:00) New York
// time on a trading day.
bool IsMarketOpen(absl::Time time);

}  // namespace firefly

#endif  // FIREFLY_TRADING_MARKET_CALENDAR_H_
