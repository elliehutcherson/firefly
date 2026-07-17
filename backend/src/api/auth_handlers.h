#ifndef FIREFLY_API_AUTH_HANDLERS_H_
#define FIREFLY_API_AUTH_HANDLERS_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "nlohmann/json.hpp"
#include "auth/session_store.h"
#include "auth/turnstile.h"
#include "auth/user_store.h"
#include "common/clock.h"
#include "db/db.h"

namespace firefly {

// Auth endpoint cores, kept crow-free like market_handlers: plain strings in
// (request body JSON, cookie token, client IP), JSON + token intents out.
// server.cc owns cookies, headers, and HTTP framing.

// All borrowed, all non-owning.
struct AuthDeps {
  Db& db;
  UserStore& users;
  SessionStore& sessions;
  TurnstileVerifier& turnstile;
  const Clock& clock;
  absl::Duration session_ttl = absl::Hours(24 * 30);
  int signup_ip_daily_cap = 3;
};

// What the route wrapper should do with the session cookie.
struct AuthResult {
  nlohmann::json body;
  // Non-empty: set ff_session to this value (signup/login).
  std::string set_session_token;
  // True: clear the cookie (logout).
  bool clear_session = false;
};

// The injection boundary for usernames: [A-Za-z0-9_]{3,20}, returned
// verbatim (display case preserved; uniqueness is case-insensitive).
absl::StatusOr<std::string> NormalizeUsername(const std::string& raw);

// Shape-checks a forwarded-header IP ([0-9a-fA-F:.]{1,45}) before it is
// trusted anywhere (::inet binds, Turnstile). nullopt when it fails.
std::optional<std::string> SanitizeClientIp(const std::string& raw);

// Body: {"username","password","email"?,"turnstile_token"?}. Creates the
// user and a session. AlreadyExists (409) on duplicate username,
// ResourceExhausted (429) past the per-IP daily cap, PermissionDenied (403)
// on Turnstile rejection.
absl::StatusOr<AuthResult> Signup(const AuthDeps& deps,
                                  const std::string& body_json,
                                  const std::optional<std::string>& client_ip);

// Body: {"username","password","turnstile_token"?}. Uniform
// Unauthenticated (401) for unknown user and wrong password.
absl::StatusOr<AuthResult> Login(const AuthDeps& deps,
                                 const std::string& body_json,
                                 const std::optional<std::string>& client_ip);

// Deletes the session; succeeds even when it was already gone.
absl::StatusOr<AuthResult> Logout(const AuthDeps& deps,
                                  const std::string& cookie_token);

// Profile behind the session: {"user_id","username","email","cash_cents"}.
// Touches the session (sliding TTL) at most once an hour.
absl::StatusOr<nlohmann::json> GetMeJson(const AuthDeps& deps,
                                         const std::string& cookie_token);

}  // namespace firefly

#endif  // FIREFLY_API_AUTH_HANDLERS_H_
