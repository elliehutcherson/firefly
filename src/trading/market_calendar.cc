#include "src/trading/market_calendar.h"

#include <cstdint>
#include <initializer_list>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/clock.h"

namespace firefly {
namespace {

// Regular session bounds, minutes since New York midnight: [09:30, 16:00).
constexpr int kSessionOpenMinutes = 9 * 60 + 30;
constexpr int kSessionCloseMinutes = 16 * 60;

// Fixed-date holidays shift to the nearest weekday when they land on a
// weekend (see the header for the year-boundary deviation this implies).
absl::CivilDay ObservedDay(absl::CivilDay holiday) {
  switch (absl::GetWeekday(holiday)) {
    case absl::Weekday::saturday:
      return holiday - 1;
    case absl::Weekday::sunday:
      return holiday + 1;
    default:
      return holiday;
  }
}

absl::CivilDay NthWeekdayOfMonth(int64_t year, int month, absl::Weekday weekday,
                                 int n) {
  const absl::CivilDay first(year, month, 1);
  return absl::NextWeekday(first - 1, weekday) + 7 * (n - 1);
}

absl::CivilDay LastWeekdayOfMonth(int64_t year, int month,
                                  absl::Weekday weekday) {
  const absl::CivilDay next_month(year, month + 1, 1);  // normalizes past Dec
  return absl::PrevWeekday(next_month, weekday);
}

// Anonymous Gregorian computus (Meeus, "Astronomical Algorithms" ch. 8).
// Exact for all Gregorian years; spot-checked in tests through the 2038
// April 25 latest-possible-Easter edge.
absl::CivilDay EasterSunday(int64_t year) {
  const int64_t a = year % 19;
  const int64_t b = year / 100;
  const int64_t c = year % 100;
  const int64_t d = b / 4;
  const int64_t e = b % 4;
  const int64_t f = (b + 8) / 25;
  const int64_t g = (b - f + 1) / 3;
  const int64_t h = (19 * a + b - d - g + 15) % 30;
  const int64_t i = c / 4;
  const int64_t k = c % 4;
  const int64_t l = (32 + 2 * e + 2 * i - h - k) % 7;
  const int64_t m = (a + 11 * h + 22 * l) / 451;
  const int64_t month = (h + l - 7 * m + 114) / 31;
  const int64_t day = (h + l - 7 * m + 114) % 31 + 1;
  return absl::CivilDay(year, month, day);
}

}  // namespace

bool IsNyseHoliday(absl::CivilDay day) {
  const int64_t year = day.year();
  // Next year's New Year's Day, observed on a Saturday, closes this Dec 31.
  if (day == ObservedDay(absl::CivilDay(year + 1, 1, 1))) return true;
  for (const absl::CivilDay holiday : {
           ObservedDay(absl::CivilDay(year, 1, 1)),
           NthWeekdayOfMonth(year, 1, absl::Weekday::monday, 3),  // MLK
           NthWeekdayOfMonth(year, 2, absl::Weekday::monday, 3),  // Washington
           EasterSunday(year) - 2,                                // Good Friday
           LastWeekdayOfMonth(year, 5, absl::Weekday::monday),    // Memorial
           ObservedDay(absl::CivilDay(year, 6, 19)),              // Juneteenth
           ObservedDay(absl::CivilDay(year, 7, 4)),
           NthWeekdayOfMonth(year, 9, absl::Weekday::monday, 1),  // Labor
           NthWeekdayOfMonth(year, 11, absl::Weekday::thursday, 4),
           ObservedDay(absl::CivilDay(year, 12, 25)),
       }) {
    if (day == holiday) return true;
  }
  return false;
}

bool IsTradingDay(absl::CivilDay day) {
  const absl::Weekday weekday = absl::GetWeekday(day);
  if (weekday == absl::Weekday::saturday ||
      weekday == absl::Weekday::sunday) {
    return false;
  }
  return !IsNyseHoliday(day);
}

bool IsMarketOpen(absl::Time time) {
  const absl::CivilSecond now = absl::ToCivilSecond(time, NewYorkTimeZone());
  if (!IsTradingDay(absl::CivilDay(now))) return false;
  const int minutes = static_cast<int>(now.hour()) * 60 +
                      static_cast<int>(now.minute());
  return minutes >= kSessionOpenMinutes && minutes < kSessionCloseMinutes;
}

}  // namespace firefly
