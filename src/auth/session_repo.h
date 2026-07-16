#ifndef FIREFLY_AUTH_SESSION_REPO_H_
#define FIREFLY_AUTH_SESSION_REPO_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/auth/session_store.h"
#include "src/db/db.h"

namespace firefly {

// Reads and writes over the sessions table. All methods take the SHA-256 of
// the session token as 64 lowercase hex chars (crypto.h Sha256Hex) — the raw
// token never reaches this layer, and this repo owns the \x-hex bytea
// framing. Time always arrives from the caller's injected Clock; SQL now()
// is never used, so expiry logic is testable.
class SessionRepo : public SessionStore {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit SessionRepo(Db* db) : db_(db) {}

  absl::Status CreateSession(const std::string& token_sha256_hex,
                             int64_t user_id, absl::Time now,
                             absl::Time expires_at,
                             const std::optional<std::string>& ip) override;

  absl::Status CreateSession(Transaction& transaction,
                             const std::string& token_sha256_hex,
                             int64_t user_id, absl::Time now,
                             absl::Time expires_at,
                             const std::optional<std::string>& ip) override;

  // The session behind the hash, or nullopt when absent or expired at `now`.
  absl::StatusOr<std::optional<SessionRecord>> FindSession(
      const std::string& token_sha256_hex, absl::Time now) override;

  // Sliding renewal: bumps last_seen_at and expires_at.
  absl::Status TouchSession(const std::string& token_sha256_hex,
                            absl::Time now,
                            absl::Time new_expires_at) override;

  // True when a row was deleted; false (not an error) when already gone.
  absl::StatusOr<bool> DeleteSession(
      const std::string& token_sha256_hex) override;

  // Purge job: rows expired as of `now`; returns how many were deleted.
  absl::StatusOr<int64_t> DeleteExpiredSessions(absl::Time now) override;

 private:
  Db* const db_;
};

}  // namespace firefly

#endif  // FIREFLY_AUTH_SESSION_REPO_H_
