#ifndef FIREFLY_API_STATUS_MAPPING_H_
#define FIREFLY_API_STATUS_MAPPING_H_

#include <string>

#include "absl/status/status.h"

namespace firefly {

int HttpStatusFromCode(absl::StatusCode code);

// Safe client-facing text. Infrastructure details remain available in logs
// and internal statuses but are not reflected through the HTTP API.
std::string PublicErrorMessage(const absl::Status& status);

}  // namespace firefly

#endif  // FIREFLY_API_STATUS_MAPPING_H_
