#ifndef FIREFLY_COMMON_SYMBOL_H_
#define FIREFLY_COMMON_SYMBOL_H_

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace firefly {

// A validated ticker symbol ("AAPL", "BRK.B"). Parse() is the only way to
// construct one, so any Symbol in the program satisfies the same shape the
// database enforces (instruments_symbol_format_check in 0003): one A-Z, then
// up to nine of [A-Z.-], uppercased here from whatever case the caller had.
// This is the injection boundary — raw request strings never travel further.
//
// Converts implicitly OUT to string_view (printing, StrCat, comparisons);
// construction IN stays explicit via Parse. Use str() where an owned
// std::string is required (DbParams, JSON).
class Symbol {
 public:
  static constexpr size_t kMaxLength = 10;

  static absl::StatusOr<Symbol> Parse(absl::string_view raw);

  // NOLINTNEXTLINE(google-explicit-constructor): one-way out is the point.
  operator absl::string_view() const { return value_; }
  const std::string& str() const { return value_; }

  friend bool operator==(const Symbol& a, const Symbol& b) {
    return a.value_ == b.value_;
  }
  friend bool operator!=(const Symbol& a, const Symbol& b) { return !(a == b); }
  friend bool operator<(const Symbol& a, const Symbol& b) {
    return a.value_ < b.value_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const Symbol& symbol) {
    return H::combine(std::move(h), symbol.value_);
  }

  friend std::ostream& operator<<(std::ostream& os, const Symbol& symbol) {
    return os << symbol.value_;
  }

 private:
  explicit Symbol(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

}  // namespace firefly

#endif  // FIREFLY_COMMON_SYMBOL_H_
