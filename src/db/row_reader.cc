#include "src/db/row_reader.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "src/common/status_macros.h"

namespace firefly {

RowReader::RowReader(const Row& row, absl::string_view label)
    : row_(row), label_(label) {}

absl::StatusOr<absl::string_view> RowReader::RequiredString(
    size_t index) const {
  ASSIGN_OR_RETURN(const std::optional<absl::string_view> value,
                   OptionalString(index));
  if (!value.has_value()) {
    return absl::InternalError(
        absl::StrCat(label_, " row has NULL column ", index));
  }
  return *value;
}

absl::StatusOr<std::optional<absl::string_view>> RowReader::OptionalString(
    size_t index) const {
  if (index >= row_.columns.size()) {
    return absl::InternalError(
        absl::StrCat(label_, " row is missing column ", index));
  }
  if (!row_.columns[index].has_value()) {
    return std::nullopt;
  }
  return absl::string_view(*row_.columns[index]);
}

absl::StatusOr<int64_t> RowReader::Int64(size_t index) const {
  ASSIGN_OR_RETURN(const absl::string_view text, RequiredString(index));
  int64_t value = 0;
  if (!absl::SimpleAtoi(text, &value)) {
    return absl::InternalError(absl::StrCat(
        label_, " row has invalid int64 in column ", index, ": '", text,
        "'"));
  }
  return value;
}

absl::StatusOr<absl::CivilDay> RowReader::CivilDay(size_t index) const {
  ASSIGN_OR_RETURN(const absl::string_view text, RequiredString(index));
  absl::CivilDay day;
  if (!absl::ParseCivilTime(text, &day)) {
    return absl::InternalError(absl::StrCat(
        label_, " row has invalid date in column ", index, ": '", text, "'"));
  }
  return day;
}

}  // namespace firefly
