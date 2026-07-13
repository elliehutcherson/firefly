#include "src/common/money.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "absl/strings/str_format.h"

namespace firefly {

int64_t PriceE4FromDouble(double price) {
  return std::llround(price * static_cast<double>(kPriceScale));
}

std::string PriceE4ToString(int64_t price_e4) {
  const int64_t magnitude = price_e4 < 0 ? -price_e4 : price_e4;
  return absl::StrFormat("%s%d.%04d", price_e4 < 0 ? "-" : "",
                         magnitude / kPriceScale, magnitude % kPriceScale);
}

}  // namespace firefly
