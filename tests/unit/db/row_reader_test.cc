#include "db/row_reader.h"

#include <optional>

#include "absl/status/status.h"
#include "absl/time/civil_time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "db/db_types.h"
#include "support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

TEST(RowReaderTest, ReadsRequiredAndOptionalStrings) {
  const Row row{{"hello", std::nullopt}};
  const RowReader reader(row, "probe");

  EXPECT_THAT(reader.RequiredString(0), IsOkAndHolds("hello"));
  EXPECT_THAT(reader.OptionalString(1), IsOkAndHolds(std::nullopt));
}

TEST(RowReaderTest, RejectsMissingAndNullRequiredColumnsWithContext) {
  const Row row{{std::nullopt}};
  const RowReader reader(row, "users");

  EXPECT_THAT(reader.RequiredString(0),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("users")));
  EXPECT_THAT(reader.RequiredString(1),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("column 1")));
}

TEST(RowReaderTest, ParsesInt64AndCivilDay) {
  const Row row{{"9223372036854775807", "2026-07-15"}};
  const RowReader reader(row, "probe");

  EXPECT_THAT(reader.Int64(0), IsOkAndHolds(INT64_C(9223372036854775807)));
  EXPECT_THAT(reader.CivilDay(1), IsOkAndHolds(absl::CivilDay(2026, 7, 15)));
}

TEST(RowReaderTest, RejectsMalformedTypedValues) {
  const Row row{{"12x", "not-a-date"}};
  const RowReader reader(row, "probe");

  EXPECT_THAT(reader.Int64(0), StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(reader.CivilDay(1), StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace firefly
