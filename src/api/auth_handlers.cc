#include "src/api/auth_handlers.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "nlohmann/json.hpp"
#include "src/auth/crypto.h"
#include "src/auth/session_store.h"
#include "src/auth/user_store.h"
#include "src/common/status_macros.h"
#include "src/db/transaction.h"

namespace firefly {
namespace {

constexpr size_t kMinUsername = 3;
constexpr size_t kMaxUsername = 20;
constexpr size_t kMinPassword = 8;
constexpr size_t kMaxPassword = 128;
constexpr size_t kMaxEmail = 254;
constexpr size_t kMaxClientIp = 45;  // Longest textual IPv6.

// Renew the sliding session window at most this often; bounds session
// writes to one per hour however chatty the client is.
constexpr absl::Duration kTouchInterval = absl::Hours(1);

struct Credentials {
  std::string username;
  std::string password;
  std::optional<std::string> email;
  std::string turnstile_token;  // Empty when absent; fine when disabled.
};

absl::StatusOr<std::string> GetRequiredString(const nlohmann::json& body,
                                              const char* field) {
  if (!body.contains(field) || !body[field].is_string()) {
    return absl::InvalidArgumentError(
        absl::StrCat("missing string field: ", field));
  }
  return body[field].get<std::string>();
}

// Lightweight email shape check; deliverability is the real validator once
// password recovery exists.
absl::Status ValidateEmail(const std::string& email) {
  const size_t at = email.find('@');
  if (email.size() > kMaxEmail || at == std::string::npos || at == 0 ||
      at == email.size() - 1 || email.find('@', at + 1) != std::string::npos) {
    return absl::InvalidArgumentError("invalid email");
  }
  return absl::OkStatus();
}

absl::StatusOr<Credentials> ParseCredentials(const std::string& body_json) {
  const nlohmann::json body = nlohmann::json::parse(
      body_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (body.is_discarded() || !body.is_object()) {
    return absl::InvalidArgumentError("request body must be a JSON object");
  }
  Credentials credentials;
  ASSIGN_OR_RETURN(const std::string raw_username,
                   GetRequiredString(body, "username"));
  ASSIGN_OR_RETURN(credentials.username, NormalizeUsername(raw_username));
  ASSIGN_OR_RETURN(credentials.password, GetRequiredString(body, "password"));
  if (credentials.password.size() < kMinPassword ||
      credentials.password.size() > kMaxPassword) {
    return absl::InvalidArgumentError(absl::StrCat(
        "password must be ", kMinPassword, "-", kMaxPassword, " bytes"));
  }
  if (body.contains("email") && body["email"].is_string() &&
      !body["email"].get<std::string>().empty()) {
    const std::string email = body["email"].get<std::string>();
    RETURN_IF_ERROR(ValidateEmail(email));
    credentials.email = email;
  }
  if (body.contains("turnstile_token") && body["turnstile_token"].is_string()) {
    credentials.turnstile_token = body["turnstile_token"].get<std::string>();
  }
  return credentials;
}

// Creates a session and returns the raw token; only its SHA-256 is stored.
absl::StatusOr<std::string> StartSession(
    const AuthDeps& deps, int64_t user_id,
    const std::optional<std::string>& client_ip) {
  const std::string token = GenerateSessionToken();
  const absl::Time now = deps.clock.Now();
  RETURN_IF_ERROR(deps.sessions.CreateSession(
      Sha256Hex(token), user_id, now, now + deps.session_ttl, client_ip));
  return token;
}

absl::StatusOr<std::string> StartSession(
    const AuthDeps& deps, Transaction& transaction, int64_t user_id,
    const std::optional<std::string>& client_ip) {
  const std::string token = GenerateSessionToken();
  const absl::Time now = deps.clock.Now();
  RETURN_IF_ERROR(deps.sessions.CreateSession(
      transaction, Sha256Hex(token), user_id, now, now + deps.session_ttl,
      client_ip));
  return token;
}

// Session lookup shared by /me (and, later, everything authenticated).
// Renews the sliding window at most once per kTouchInterval.
absl::StatusOr<SessionRecord> RequireSession(const AuthDeps& deps,
                                             const std::string& cookie_token) {
  if (cookie_token.empty()) {
    return absl::UnauthenticatedError("not signed in");
  }
  const std::string token_hash = Sha256Hex(cookie_token);
  const absl::Time now = deps.clock.Now();
  ASSIGN_OR_RETURN(const std::optional<SessionRecord> session,
                   deps.sessions.FindSession(token_hash, now));
  if (!session.has_value()) {
    return absl::UnauthenticatedError("not signed in");
  }
  if (now - session->last_seen_at > kTouchInterval) {
    RETURN_IF_ERROR(
        deps.sessions.TouchSession(token_hash, now, now + deps.session_ttl));
  }
  return *session;
}

}  // namespace

absl::StatusOr<std::string> NormalizeUsername(const std::string& raw) {
  if (raw.size() < kMinUsername || raw.size() > kMaxUsername) {
    return absl::InvalidArgumentError(absl::StrCat(
        "username must be ", kMinUsername, "-", kMaxUsername,
        " characters of [A-Za-z0-9_]"));
  }
  for (const char c : raw) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) {
      return absl::InvalidArgumentError(
          "username may only contain letters, digits, and underscore");
    }
  }
  return raw;
}

std::optional<std::string> SanitizeClientIp(const std::string& raw) {
  if (raw.empty() || raw.size() > kMaxClientIp) {
    return std::nullopt;
  }
  for (const char c : raw) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F') || c == ':' || c == '.';
    if (!ok) {
      return std::nullopt;
    }
  }
  return raw;
}

absl::StatusOr<AuthResult> Signup(const AuthDeps& deps,
                                  const std::string& body_json,
                                  const std::optional<std::string>& client_ip) {
  ASSIGN_OR_RETURN(const Credentials credentials, ParseCredentials(body_json));
  RETURN_IF_ERROR(
      deps.turnstile.Verify(credentials.turnstile_token, client_ip));
  if (client_ip.has_value()) {
    ASSIGN_OR_RETURN(
        const int64_t recent,
        deps.users.CountRecentSignups(
            *client_ip, deps.clock.Now() - absl::Hours(24)));
    if (recent >= deps.signup_ip_daily_cap) {
      return absl::ResourceExhaustedError("too many signups from this address");
    }
  }
  ASSIGN_OR_RETURN(const std::string password_hash,
                   HashPassword(credentials.password));
  ASSIGN_OR_RETURN(std::unique_ptr<Transaction> transaction, deps.db.Begin());
  const absl::StatusOr<int64_t> user_id = deps.users.CreateUser(
      *transaction, credentials.username, password_hash, credentials.email,
      client_ip);
  if (absl::IsAlreadyExists(user_id.status())) {
    return absl::AlreadyExistsError("username is taken");
  }
  RETURN_IF_ERROR(user_id.status());
  ASSIGN_OR_RETURN(const std::string token,
                   StartSession(deps, *transaction, *user_id, client_ip));
  RETURN_IF_ERROR(transaction->Commit());
  return AuthResult{
      .body = {{"user_id", *user_id}, {"username", credentials.username}},
      .set_session_token = token};
}

absl::StatusOr<AuthResult> Login(const AuthDeps& deps,
                                 const std::string& body_json,
                                 const std::optional<std::string>& client_ip) {
  ASSIGN_OR_RETURN(const Credentials credentials, ParseCredentials(body_json));
  RETURN_IF_ERROR(
      deps.turnstile.Verify(credentials.turnstile_token, client_ip));
  ASSIGN_OR_RETURN(const std::optional<UserRecord> user,
                   deps.users.FindUserByUsername(credentials.username));
  // Uniform failure for unknown user and wrong password.
  if (!user.has_value() ||
      !VerifyPassword(user->password_hash, credentials.password)) {
    return absl::UnauthenticatedError("invalid username or password");
  }
  ASSIGN_OR_RETURN(const std::string token,
                   StartSession(deps, user->id, client_ip));
  return AuthResult{
      .body = {{"user_id", user->id}, {"username", user->username}},
      .set_session_token = token};
}

absl::StatusOr<AuthResult> Logout(const AuthDeps& deps,
                                  const std::string& cookie_token) {
  if (!cookie_token.empty()) {
    RETURN_IF_ERROR(
        deps.sessions.DeleteSession(Sha256Hex(cookie_token)).status());
  }
  return AuthResult{.body = {{"ok", true}}, .clear_session = true};
}

absl::StatusOr<nlohmann::json> GetMeJson(const AuthDeps& deps,
                                         const std::string& cookie_token) {
  ASSIGN_OR_RETURN(const SessionRecord session,
                   RequireSession(deps, cookie_token));
  ASSIGN_OR_RETURN(const UserProfile profile,
                   deps.users.GetUser(session.user_id));
  nlohmann::json body = {{"user_id", profile.id},
                         {"username", profile.username},
                         {"cash_cents", profile.cash_cents}};
  body["email"] = profile.email.has_value()
                      ? nlohmann::json(*profile.email)
                      : nlohmann::json(nullptr);
  return body;
}

}  // namespace firefly
