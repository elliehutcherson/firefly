#ifndef FIREFLY_TESTS_STATUS_MATCHERS_H_
#define FIREFLY_TESTS_STATUS_MATCHERS_H_

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// gtest-style shorthands over abseil's status matchers (abseil ships
// absl_testing::IsOk()/StatusIs()/IsOkAndHolds() but not EXPECT_OK). Both
// accept absl::Status and absl::StatusOr<T>. For error checks, prefer
// matching the code:
//
//   EXPECT_THAT(result, absl_testing::StatusIs(absl::StatusCode::kNotFound));

#define EXPECT_OK(expr) EXPECT_THAT((expr), ::absl_testing::IsOk())
#define ASSERT_OK(expr) ASSERT_THAT((expr), ::absl_testing::IsOk())

#endif  // FIREFLY_TESTS_STATUS_MATCHERS_H_
