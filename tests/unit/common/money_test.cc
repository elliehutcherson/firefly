#include "src/common/money.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/support/status_matchers.h"

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

TEST(DebitCreditCentsTest, ExactCentsRoundTheSame) {
  // $189.9550 x 2 = $379.91 exactly; no sub-cent remainder to round.
  EXPECT_THAT(DebitCents(1899550, 2), IsOkAndHolds(37991));
  EXPECT_THAT(CreditCents(1899550, 2), IsOkAndHolds(37991));
  EXPECT_THAT(DebitCents(10000, 1), IsOkAndHolds(100));
  EXPECT_THAT(CreditCents(10000, 1), IsOkAndHolds(100));
}

TEST(DebitCreditCentsTest, SubCentRoundsAgainstTheAccount) {
  // $10.0001 x 3 = $30.0003: the debit rounds up, the credit rounds down.
  EXPECT_THAT(DebitCents(100001, 3), IsOkAndHolds(3001));
  EXPECT_THAT(CreditCents(100001, 3), IsOkAndHolds(3000));
  // A single e4 unit still costs a whole cent to buy and pays nothing to sell.
  EXPECT_THAT(DebitCents(1, 1), IsOkAndHolds(1));
  EXPECT_THAT(CreditCents(1, 1), IsOkAndHolds(0));
}

TEST(DebitCreditCentsTest, RoundTripNeverProfitable) {
  for (const int64_t price_e4 : {1LL, 99LL, 100LL, 101LL, 9999LL, 10000LL,
                                 100001LL, 1899550LL, 12345678901LL}) {
    for (const int64_t quantity : {1LL, 3LL, 7LL, 1000000LL}) {
      const auto debit = DebitCents(price_e4, quantity);
      const auto credit = CreditCents(price_e4, quantity);
      ASSERT_OK(debit);
      ASSERT_OK(credit);
      EXPECT_GE(*debit, *credit)
          << "price_e4=" << price_e4 << " quantity=" << quantity;
    }
  }
}

TEST(DebitCreditCentsTest, RejectsNonPositiveInputs) {
  for (const auto& [price_e4, quantity] :
       {std::pair<int64_t, int64_t>{0, 1}, {-10000, 1}, {10000, 0},
        {10000, -1}}) {
    EXPECT_THAT(DebitCents(price_e4, quantity),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(CreditCents(price_e4, quantity),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST(DebitCreditCentsTest, RejectsOverflow) {
  const int64_t max = std::numeric_limits<int64_t>::max();
  EXPECT_THAT(DebitCents(max, 2),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(CreditCents(max, 2),
              StatusIs(absl::StatusCode::kInvalidArgument));
  // The ceiling adjustment must not overflow either: max is not divisible by
  // 100, so the debit's +99 would wrap while the floor'd credit is fine.
  EXPECT_THAT(DebitCents(max, 1),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(CreditCents(max, 1), IsOkAndHolds(max / 100));
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
