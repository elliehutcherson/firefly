#include "src/api/status_mapping.h"

#include <string>

#include "absl/status/status.h"

namespace firefly {

int HttpStatusFromCode(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kInvalidArgument:
      return 400;
    case absl::StatusCode::kUnauthenticated:
      return 401;
    case absl::StatusCode::kPermissionDenied:
      return 403;
    case absl::StatusCode::kNotFound:
      return 404;
    case absl::StatusCode::kAlreadyExists:
      return 409;
    // State rejections (crossing zero, insufficient cash/margin, market
    // closed): the request was well-formed but the account/world state
    // refuses it.
    case absl::StatusCode::kFailedPrecondition:
      return 422;
    case absl::StatusCode::kResourceExhausted:
      return 429;
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kUnavailable:
      return 503;
    case absl::StatusCode::kDeadlineExceeded:
      return 504;
    default:
      return 500;
  }
}

std::string PublicErrorMessage(const absl::Status& status) {
  switch (status.code()) {
    case absl::StatusCode::kInternal:
    case absl::StatusCode::kUnknown:
    case absl::StatusCode::kDataLoss:
      return "internal server error";
    case absl::StatusCode::kAborted:
      return "request could not be completed; retry";
    case absl::StatusCode::kUnavailable:
      return "service unavailable";
    case absl::StatusCode::kDeadlineExceeded:
      return "request timed out";
    default:
      return std::string(status.message());
  }
}

}  // namespace firefly
