#ifndef FIREFLY_AUTH_SESSION_AUTH_H_
#define FIREFLY_AUTH_SESSION_AUTH_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/auth/session_store.h"
#include "src/common/clock.h"

namespace firefly {

// The narrow dependency bundle for session authentication; auth and trading
// handlers both consume it. All borrowed, all non-owning.
struct SessionAuth {
  SessionStore& sessions;
  const Clock& clock;
  absl::Duration session_ttl = absl::Hours(24 * 30);
};

// Session lookup shared by every authenticated endpoint: Unauthenticated
// unless the cookie token maps to a live session. Renews the sliding
// expiry window at most once per hour, so session writes stay bounded
// however chatty the client is.
absl::StatusOr<SessionRecord> RequireSession(const SessionAuth& auth,
                                             const std::string& cookie_token);

}  // namespace firefly

#endif  // FIREFLY_AUTH_SESSION_AUTH_H_
