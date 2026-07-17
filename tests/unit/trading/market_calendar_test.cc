#include "src/trading/market_calendar.h"

#include <vector>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/common/clock.h"

namespace firefly {
namespace {

using ::testing::ElementsAreArray;

// Every holiday the calendar reports in `year`, by exhaustive scan.
std::vector<absl::CivilDay> HolidaysIn(int64_t year) {
  std::vector<absl::CivilDay> holidays;
  for (absl::CivilDay day(year, 1, 1); day.year() == year; ++day) {
    if (IsNyseHoliday(day)) holidays.push_back(day);
  }
  return holidays;
}

TEST(IsNyseHolidayTest, Full2025List) {
  EXPECT_THAT(HolidaysIn(2025),
              ElementsAreArray({
                  absl::CivilDay(2025, 1, 1),    // New Year's Day (Wed)
                  absl::CivilDay(2025, 1, 20),   // MLK Day
                  absl::CivilDay(2025, 2, 17),   // Washington's Birthday
                  absl::CivilDay(2025, 4, 18),   // Good Friday
                  absl::CivilDay(2025, 5, 26),   // Memorial Day
                  absl::CivilDay(2025, 6, 19),   // Juneteenth (Thu)
                  absl::CivilDay(2025, 7, 4),    // Independence Day (Fri)
                  absl::CivilDay(2025, 9, 1),    // Labor Day
                  absl::CivilDay(2025, 11, 27),  // Thanksgiving
                  absl::CivilDay(2025, 12, 25),  // Christmas (Thu)
              }));
}

TEST(IsNyseHolidayTest, Full2026List) {
  EXPECT_THAT(HolidaysIn(2026),
              ElementsAreArray({
                  absl::CivilDay(2026, 1, 1),    // New Year's Day (Thu)
                  absl::CivilDay(2026, 1, 19),   // MLK Day
                  absl::CivilDay(2026, 2, 16),   // Washington's Birthday
                  absl::CivilDay(2026, 4, 3),    // Good Friday
                  absl::CivilDay(2026, 5, 25),   // Memorial Day
                  absl::CivilDay(2026, 6, 19),   // Juneteenth (Fri)
                  absl::CivilDay(2026, 7, 3),    // Jul 4 is Sat -> Fri
                  absl::CivilDay(2026, 9, 7),    // Labor Day
                  absl::CivilDay(2026, 11, 26),  // Thanksgiving
                  absl::CivilDay(2026, 12, 25),  // Christmas (Fri)
              }));
}

TEST(IsNyseHolidayTest, ObservanceShiftsIn2027) {
  // 2027 exercises every observance rule: Juneteenth and Christmas fall on
  // Saturday (observed Friday), July 4 on Sunday (observed Monday), and New
  // Year's Day 2028 on Saturday, closing 2027-12-31 — the documented
  // deviation where the real NYSE stays open.
  EXPECT_THAT(HolidaysIn(2027),
              ElementsAreArray({
                  absl::CivilDay(2027, 1, 1),    // New Year's Day (Fri)
                  absl::CivilDay(2027, 1, 18),   // MLK Day
                  absl::CivilDay(2027, 2, 15),   // Washington's Birthday
                  absl::CivilDay(2027, 3, 26),   // Good Friday
                  absl::CivilDay(2027, 5, 31),   // Memorial Day
                  absl::CivilDay(2027, 6, 18),   // Juneteenth: Sat -> Fri
                  absl::CivilDay(2027, 7, 5),    // Jul 4: Sun -> Mon
                  absl::CivilDay(2027, 9, 6),    // Labor Day
                  absl::CivilDay(2027, 11, 25),  // Thanksgiving
                  absl::CivilDay(2027, 12, 24),  // Christmas: Sat -> Fri
                  absl::CivilDay(2027, 12, 31),  // New Year's 2028: Sat -> Fri
              }));
}

TEST(IsNyseHolidayTest, GoodFridayTracksComputedEaster) {
  // Pinned against the published Easter table; Good Friday = Easter - 2.
  // 2038 is the latest possible Easter (April 25), the algorithm's edge.
  for (const absl::CivilDay good_friday : {
           absl::CivilDay(2024, 3, 29),
           absl::CivilDay(2025, 4, 18),
           absl::CivilDay(2026, 4, 3),
           absl::CivilDay(2027, 3, 26),
           absl::CivilDay(2028, 4, 14),
           absl::CivilDay(2029, 3, 30),
           absl::CivilDay(2030, 4, 19),
           absl::CivilDay(2031, 4, 11),
           absl::CivilDay(2038, 4, 23),
       }) {
    EXPECT_TRUE(IsNyseHoliday(good_friday)) << good_friday;
    EXPECT_EQ(absl::GetWeekday(good_friday), absl::Weekday::friday)
        << good_friday;
  }
}

TEST(IsTradingDayTest, WeekendsAndHolidaysAreNotTradingDays) {
  EXPECT_FALSE(IsTradingDay(absl::CivilDay(2026, 7, 18)));  // Saturday
  EXPECT_FALSE(IsTradingDay(absl::CivilDay(2026, 7, 19)));  // Sunday
  EXPECT_FALSE(IsTradingDay(absl::CivilDay(2026, 7, 3)));   // observed holiday
  EXPECT_TRUE(IsTradingDay(absl::CivilDay(2026, 7, 17)));   // regular Friday
  EXPECT_TRUE(IsTradingDay(absl::CivilDay(2026, 7, 20)));   // regular Monday
}

absl::Time NewYork(absl::CivilSecond civil) {
  return absl::FromCivil(civil, NewYorkTimeZone());
}

TEST(IsMarketOpenTest, SessionBoundariesToTheSecond) {
  // Wednesday 2026-07-15, a regular trading day.
  EXPECT_FALSE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 7, 15, 9, 29, 59))));
  EXPECT_TRUE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 7, 15, 9, 30, 0))));
  EXPECT_TRUE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 7, 15, 15, 59, 59))));
  EXPECT_FALSE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 7, 15, 16, 0, 0))));
}

TEST(IsMarketOpenTest, ClosedAllDayOnWeekendsAndHolidays) {
  // Good Friday 2026-04-03 and the following Saturday, at midday.
  EXPECT_FALSE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 4, 3, 12, 0, 0))));
  EXPECT_FALSE(IsMarketOpen(NewYork(absl::CivilSecond(2026, 4, 4, 12, 0, 0))));
}

TEST(IsMarketOpenTest, TracksNewYorkAcrossDstTransitions) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  // Friday 2026-03-06 (EST, UTC-5): open is 14:30 UTC, not 13:30.
  EXPECT_FALSE(
      IsMarketOpen(absl::FromCivil(absl::CivilSecond(2026, 3, 6, 13, 30, 0), utc)));
  EXPECT_TRUE(
      IsMarketOpen(absl::FromCivil(absl::CivilSecond(2026, 3, 6, 14, 30, 0), utc)));
  // Monday 2026-03-09, after the Mar 8 spring-forward (EDT, UTC-4): open is
  // 13:30 UTC, and 20:30 UTC is past the close.
  EXPECT_TRUE(
      IsMarketOpen(absl::FromCivil(absl::CivilSecond(2026, 3, 9, 13, 30, 0), utc)));
  EXPECT_FALSE(
      IsMarketOpen(absl::FromCivil(absl::CivilSecond(2026, 3, 9, 20, 30, 0), utc)));
}

}  // namespace
}  // namespace firefly
