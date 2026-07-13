#include "src/common/money.h"

#include "gtest/gtest.h"

namespace firefly {
namespace {

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

}  // namespace
}  // namespace firefly
