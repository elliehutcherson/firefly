#ifndef FIREFLY_COMMON_STATUS_MACROS_H_
#define FIREFLY_COMMON_STATUS_MACROS_H_

#include <utility>

#include "absl/status/status.h"

// The two sanctioned macros for absl::Status control flow (see docs/STYLE.md).
//
//   RETURN_IF_ERROR(ExpressionReturningStatus());
//
//   ASSIGN_OR_RETURN(auto value, ExpressionReturningStatusOr());
//
// Both return the error Status from the enclosing function on failure, so the
// enclosing function must return absl::Status or absl::StatusOr<T>.
// ASSIGN_OR_RETURN declares variables, so it cannot be used as the body of an
// unbraced if/else.

#define RETURN_IF_ERROR(expr)          \
  do {                                 \
    absl::Status _status = (expr);     \
    if (!_status.ok()) return _status; \
  } while (false)

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  FIREFLY_ASSIGN_OR_RETURN_IMPL(     \
      FIREFLY_STATUS_MACROS_CONCAT(_status_or_, __LINE__), lhs, rexpr)

// Implementation details.
#define FIREFLY_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                  \
  if (!statusor.ok()) return std::move(statusor).status();  \
  lhs = std::move(statusor).value()

#define FIREFLY_STATUS_MACROS_CONCAT(x, y) \
  FIREFLY_STATUS_MACROS_CONCAT_INNER(x, y)
#define FIREFLY_STATUS_MACROS_CONCAT_INNER(x, y) x##y

#endif  // FIREFLY_COMMON_STATUS_MACROS_H_
