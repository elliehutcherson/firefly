#include "src/auth/session_auth.h"

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/auth/crypto.h"
#include "src/auth/session_store.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

// Renew the sliding session window at most this often; bounds session
// writes to one per hour however chatty the client is.
constexpr absl::Duration kTouchInterval = absl::Hours(1);

}  // namespace

absl::StatusOr<SessionRecord> RequireSession(const SessionAuth& auth,
                                             const std::string& cookie_token) {
  if (cookie_token.empty()) {
    return absl::UnauthenticatedError("not signed in");
  }
  const std::string token_hash = Sha256Hex(cookie_token);
  const absl::Time now = auth.clock.Now();
  ASSIGN_OR_RETURN(const std::optional<SessionRecord> session,
                   auth.sessions.FindSession(token_hash, now));
  if (!session.has_value()) {
    return absl::UnauthenticatedError("not signed in");
  }
  if (now - session->last_seen_at > kTouchInterval) {
    RETURN_IF_ERROR(
        auth.sessions.TouchSession(token_hash, now, now + auth.session_ttl));
  }
  return *session;
}

}  // namespace firefly
