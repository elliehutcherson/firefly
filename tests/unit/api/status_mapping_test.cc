#include "src/api/status_mapping.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace firefly {
namespace {

TEST(HttpStatusFromCodeTest, MapsCodes) {
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kInvalidArgument), 400);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnauthenticated), 401);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kPermissionDenied), 403);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kNotFound), 404);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kAlreadyExists), 409);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kResourceExhausted), 429);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kAborted), 503);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnavailable), 503);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kDeadlineExceeded), 504);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kInternal), 500);
  EXPECT_EQ(HttpStatusFromCode(absl::StatusCode::kUnknown), 500);
}

TEST(PublicErrorMessageTest, HidesInfrastructureDetails) {
  EXPECT_EQ(PublicErrorMessage(
                absl::InternalError("relation users does not exist")),
            "internal server error");
  EXPECT_EQ(PublicErrorMessage(
                absl::UnavailableError("connection to 10.0.0.4 failed")),
            "service unavailable");
  EXPECT_EQ(PublicErrorMessage(absl::AbortedError("sqlstate 40001")),
            "request could not be completed; retry");
}

TEST(PublicErrorMessageTest, PreservesDomainErrors) {
  EXPECT_EQ(PublicErrorMessage(absl::NotFoundError("unknown symbol: NOPE")),
            "unknown symbol: NOPE");
  EXPECT_EQ(PublicErrorMessage(absl::InvalidArgumentError("invalid symbol")),
            "invalid symbol");
}

}  // namespace
}  // namespace firefly
