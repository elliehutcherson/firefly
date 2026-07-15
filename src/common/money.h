#ifndef FIREFLY_COMMON_MONEY_H_
#define FIREFLY_COMMON_MONEY_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// Fixed-point money conventions (see docs/STYLE.md, "Integer types"):
//
//   * Cash amounts are integer cents: int64_t fields named *_cents.
//   * Prices are e4 fixed point (1/10000 dollar): int64_t fields named *_e4.
//     This matches the database's NUMERIC(14,4) exactly, so prices round-trip
//     through Postgres without loss.
//
// Floating point never stores or computes money; doubles appear only at the
// JSON boundary, converted immediately via PriceE4FromDouble.

namespace firefly {

inline constexpr int64_t kPriceScale = 10000;

// Rounds to the nearest 1/10000 dollar. For prices arriving as JSON numbers.
int64_t PriceE4FromDouble(double price);

// Formats with exactly four decimals ("189.9550"), the text form Postgres
// NUMERIC(14,4) accepts.
std::string PriceE4ToString(int64_t price_e4);

// Parses the text form Postgres NUMERIC(14,4) emits ("189.9550", "12",
// "-0.005"). InvalidArgument on junk, more than four decimals, or overflow.
// The inverse of PriceE4ToString; how prices come back from Db::Query.
absl::StatusOr<int64_t> PriceE4FromString(absl::string_view text);

}  // namespace firefly

#endif  // FIREFLY_COMMON_MONEY_H_
