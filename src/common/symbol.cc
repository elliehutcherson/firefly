#include "src/common/symbol.h"

#include <cctype>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace firefly {
namespace {

bool IsValidChar(char c, bool first) {
  return (c >= 'A' && c <= 'Z') || (!first && (c == '.' || c == '-'));
}

}  // namespace

absl::StatusOr<Symbol> Symbol::Parse(absl::string_view raw) {
  if (raw.empty() || raw.size() > kMaxLength) {
    return absl::InvalidArgumentError("invalid symbol");
  }
  std::string value(raw);
  for (char& c : value) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (!IsValidChar(value[i], i == 0)) {
      return absl::InvalidArgumentError("invalid symbol");
    }
  }
  return Symbol(std::move(value));
}

}  // namespace firefly
