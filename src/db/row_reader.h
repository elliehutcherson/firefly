#ifndef FIREFLY_DB_ROW_READER_H_
#define FIREFLY_DB_ROW_READER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "db/db_types.h"

namespace firefly {

// Checked, typed access to one database result row. Returned string views
// borrow from the Row and remain valid only while that Row is alive.
class RowReader {
 public:
  RowReader(const Row& row, absl::string_view label);

  absl::StatusOr<absl::string_view> RequiredString(size_t index) const;
  absl::StatusOr<std::optional<absl::string_view>> OptionalString(
      size_t index) const;
  absl::StatusOr<int64_t> Int64(size_t index) const;
  absl::StatusOr<absl::CivilDay> CivilDay(size_t index) const;

 private:
  const Row& row_;
  const std::string label_;
};

}  // namespace firefly

#endif  // FIREFLY_DB_ROW_READER_H_
