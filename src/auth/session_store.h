#ifndef FIREFLY_AUTH_SESSION_STORE_H_
#define FIREFLY_AUTH_SESSION_STORE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "db/transaction.h"

namespace firefly {

struct SessionRecord {
  int64_t user_id = 0;
  absl::Time last_seen_at;
};

// Auth's persistence boundary for server-side sessions.
class SessionStore {
 public:
  virtual ~SessionStore() = default;

  virtual absl::Status CreateSession(
      const std::string& token_sha256_hex, int64_t user_id, absl::Time now,
      absl::Time expires_at, const std::optional<std::string>& ip) = 0;
  virtual absl::Status CreateSession(
      Transaction& transaction, const std::string& token_sha256_hex,
      int64_t user_id, absl::Time now, absl::Time expires_at,
      const std::optional<std::string>& ip) = 0;
  virtual absl::StatusOr<std::optional<SessionRecord>> FindSession(
      const std::string& token_sha256_hex, absl::Time now) = 0;
  virtual absl::Status TouchSession(const std::string& token_sha256_hex,
                                    absl::Time now,
                                    absl::Time new_expires_at) = 0;
  virtual absl::StatusOr<bool> DeleteSession(
      const std::string& token_sha256_hex) = 0;
  virtual absl::StatusOr<int64_t> DeleteExpiredSessions(absl::Time now) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_AUTH_SESSION_STORE_H_
