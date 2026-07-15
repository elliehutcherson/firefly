#include "src/common/money.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

TEST(PriceE4FromDoubleTest, RoundsToNearestTenThousandth) {
  EXPECT_EQ(PriceE4FromDouble(189.955), 1899550);
  EXPECT_EQ(PriceE4FromDouble(0.0), 0);
  EXPECT_EQ(PriceE4FromDouble(1.0), 10000);
  // 0.1 is not representable in binary; rounding must still land exactly.
  EXPECT_EQ(PriceE4FromDouble(0.1), 1000);
  EXPECT_EQ(PriceE4FromDouble(123456.7891), 1234567891);
}

TEST(PriceE4ToStringTest, AlwaysFourDecimals) {
  EXPECT_EQ(PriceE4ToString(1899550), "189.9550");
  EXPECT_EQ(PriceE4ToString(10000), "1.0000");
  EXPECT_EQ(PriceE4ToString(50), "0.0050");
  EXPECT_EQ(PriceE4ToString(0), "0.0000");
  EXPECT_EQ(PriceE4ToString(-1899550), "-189.9550");
  EXPECT_EQ(PriceE4ToString(-50), "-0.0050");
}

TEST(PriceE4FromStringTest, ParsesNumericText) {
  EXPECT_THAT(PriceE4FromString("189.9550"),
              IsOkAndHolds(1899550));
  EXPECT_THAT(PriceE4FromString("0.0000"), IsOkAndHolds(0));
  EXPECT_THAT(PriceE4FromString("12"), IsOkAndHolds(120000));
  EXPECT_THAT(PriceE4FromString("0.005"), IsOkAndHolds(50));
  EXPECT_THAT(PriceE4FromString("-189.9550"),
              IsOkAndHolds(-1899550));
  EXPECT_THAT(PriceE4FromString("-0.005"), IsOkAndHolds(-50));
}

TEST(PriceE4FromStringTest, RoundTripsToString) {
  for (const int64_t price_e4 : {0LL, 50LL, 10000LL, 1899550LL, -1899550LL}) {
    EXPECT_THAT(PriceE4FromString(PriceE4ToString(price_e4)),
                IsOkAndHolds(price_e4));
  }
}

TEST(PriceE4FromStringTest, RejectsJunk) {
  for (const char* junk : {"", "-", ".", "12.", ".5", "1.23456", "abc",
                           "1.2e3", "12a", "1 2", "+12", "--1", "1.2.3"}) {
    EXPECT_THAT(PriceE4FromString(junk),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "input: '" << junk << "'";
  }
}

TEST(PriceE4FromStringTest, RejectsOverflow) {
  // INT64_MAX is ~9.2e18; anything above ~9.2e14 dollars overflows e4.
  EXPECT_THAT(PriceE4FromString("922337203685478"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(PriceE4FromString("99999999999999999999"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace firefly
