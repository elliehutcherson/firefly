#ifndef FIREFLY_TESTS_FAKES_AUTH_FAKE_SESSION_STORE_H_
#define FIREFLY_TESTS_FAKES_AUTH_FAKE_SESSION_STORE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "auth/session_store.h"
#include "db/transaction.h"

namespace firefly {

// In-memory SessionStore keyed by token hash. Honors expiry in FindSession
// and counts touches so tests can pin the touch throttle. `outage` fails
// every call, for error-propagation tests.
class FakeSessionStore : public SessionStore {
 public:
  struct StoredSession {
    int64_t user_id = 0;
    absl::Time last_seen_at;
    absl::Time expires_at;
  };

  absl::Status CreateSession(const std::string& token_sha256_hex,
                             int64_t user_id, absl::Time now,
                             absl::Time expires_at,
                             const std::optional<std::string>& ip) override {
    if (!outage.ok()) return outage;
    sessions[token_sha256_hex] = {user_id, now, expires_at};
    return absl::OkStatus();
  }

  absl::Status CreateSession(Transaction& transaction,
                             const std::string& token_sha256_hex,
                             int64_t user_id, absl::Time now,
                             absl::Time expires_at,
                             const std::optional<std::string>& ip) override {
    return CreateSession(token_sha256_hex, user_id, now, expires_at, ip);
  }

  absl::StatusOr<std::optional<SessionRecord>> FindSession(
      const std::string& token_sha256_hex, absl::Time now) override {
    if (!outage.ok()) return outage;
    ++finds;
    const auto it = sessions.find(token_sha256_hex);
    if (it == sessions.end() || it->second.expires_at <= now) {
      return std::nullopt;
    }
    return SessionRecord{.user_id = it->second.user_id,
                         .last_seen_at = it->second.last_seen_at};
  }

  absl::Status TouchSession(const std::string& token_sha256_hex,
                            absl::Time now,
                            absl::Time new_expires_at) override {
    if (!outage.ok()) return outage;
    if (!touch_status.ok()) return touch_status;
    ++touches;
    const auto it = sessions.find(token_sha256_hex);
    if (it != sessions.end()) {
      it->second.last_seen_at = now;
      it->second.expires_at = new_expires_at;
    }
    return absl::OkStatus();
  }

  absl::StatusOr<bool> DeleteSession(
      const std::string& token_sha256_hex) override {
    if (!outage.ok()) return outage;
    return sessions.erase(token_sha256_hex) > 0;
  }

  absl::StatusOr<int64_t> DeleteExpiredSessions(absl::Time now) override {
    if (!outage.ok()) return outage;
    int64_t deleted = 0;
    for (auto it = sessions.begin(); it != sessions.end();) {
      if (it->second.expires_at <= now) {
        sessions.erase(it++);
        ++deleted;
      } else {
        ++it;
      }
    }
    return deleted;
  }

  absl::flat_hash_map<std::string, StoredSession> sessions;
  absl::Status outage;        // OK unless a test sets a store-wide failure.
  absl::Status touch_status;  // OK unless a test fails only the touch.
  int finds = 0;
  int touches = 0;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_AUTH_FAKE_SESSION_STORE_H_
