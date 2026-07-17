#include "common/money.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "common/status_macros.h"

namespace firefly {
namespace {

// One cent is 100 e4 units, so cents = price_e4 * quantity / 100.
constexpr int64_t kE4PerCent = 100;

// Shared validation and overflow-checked product for Debit/CreditCents.
absl::StatusOr<int64_t> CostE4(int64_t price_e4, int64_t quantity) {
  if (price_e4 <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("price must be positive: ", price_e4));
  }
  if (quantity <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("quantity must be positive: ", quantity));
  }
  if (price_e4 > std::numeric_limits<int64_t>::max() / quantity) {
    return absl::InvalidArgumentError(
        absl::StrCat("cost overflows int64: ", price_e4, " * ", quantity));
  }
  return price_e4 * quantity;
}

bool IsDigits(absl::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

}  // namespace

int64_t PriceE4FromDouble(double price) {
  return std::llround(price * static_cast<double>(kPriceScale));
}

std::string PriceE4ToString(int64_t price_e4) {
  const int64_t magnitude = price_e4 < 0 ? -price_e4 : price_e4;
  return absl::StrFormat("%s%d.%04d", price_e4 < 0 ? "-" : "",
                         magnitude / kPriceScale, magnitude % kPriceScale);
}

absl::StatusOr<int64_t> DebitCents(int64_t price_e4, int64_t quantity) {
  ASSIGN_OR_RETURN(const int64_t cost_e4, CostE4(price_e4, quantity));
  if (cost_e4 > std::numeric_limits<int64_t>::max() - (kE4PerCent - 1)) {
    return absl::InvalidArgumentError(
        absl::StrCat("cost overflows int64: ", price_e4, " * ", quantity));
  }
  return (cost_e4 + kE4PerCent - 1) / kE4PerCent;
}

absl::StatusOr<int64_t> CreditCents(int64_t price_e4, int64_t quantity) {
  ASSIGN_OR_RETURN(const int64_t cost_e4, CostE4(price_e4, quantity));
  return cost_e4 / kE4PerCent;
}

absl::StatusOr<int64_t> PriceE4FromString(absl::string_view text) {
  absl::string_view rest = text;
  const bool negative = absl::ConsumePrefix(&rest, "-");

  absl::string_view whole_digits = rest;
  absl::string_view frac_digits;
  if (const size_t dot = rest.find('.'); dot != absl::string_view::npos) {
    whole_digits = rest.substr(0, dot);
    frac_digits = rest.substr(dot + 1);
    if (frac_digits.empty() || frac_digits.size() > 4 ||
        !IsDigits(frac_digits)) {
      return absl::InvalidArgumentError(
          absl::StrCat("not a NUMERIC(14,4) value: '", text, "'"));
    }
  }

  int64_t whole = 0;
  if (!IsDigits(whole_digits) || !absl::SimpleAtoi(whole_digits, &whole)) {
    return absl::InvalidArgumentError(
        absl::StrCat("not a NUMERIC(14,4) value: '", text, "'"));
  }

  int64_t frac = 0;
  if (!frac_digits.empty()) {
    // IsDigits and size <= 4 make this infallible.
    (void)absl::SimpleAtoi(frac_digits, &frac);
    for (size_t i = frac_digits.size(); i < 4; ++i) {
      frac *= 10;
    }
  }

  if (whole > (std::numeric_limits<int64_t>::max() - frac) / kPriceScale) {
    return absl::InvalidArgumentError(
        absl::StrCat("price overflows e4 fixed point: '", text, "'"));
  }
  const int64_t price_e4 = (whole * kPriceScale) + frac;
  return negative ? -price_e4 : price_e4;
}

}  // namespace firefly
