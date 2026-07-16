#ifndef FIREFLY_AUTH_USER_REPO_H_
#define FIREFLY_AUTH_USER_REPO_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/auth/user_store.h"
#include "src/db/db.h"

namespace firefly {

// Reads and writes over the users table. Callers pass already-validated
// input (NormalizeUsername, HashPassword); usernames are unique
// case-insensitively via the lower(username) index — a duplicate insert
// surfaces as AlreadyExists.
class UserRepo : public UserStore {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit UserRepo(Db& db) : db_(db) {}

  // Inserts and returns the new id. `signup_ip` nullopt binds NULL.
  absl::StatusOr<int64_t> CreateUser(const std::string& username,
                                     const std::string& password_hash,
                                     const std::optional<std::string>& email,
                                     const std::optional<std::string>& signup_ip);

  // Transactional form used when user creation is one part of a larger
  // invariant, such as signup's user-plus-session write.
  absl::StatusOr<int64_t> CreateUser(
      Transaction& transaction, const std::string& username,
      const std::string& password_hash,
      const std::optional<std::string>& email,
      const std::optional<std::string>& signup_ip) override;

  // Case-insensitive lookup; nullopt when no such user.
  absl::StatusOr<std::optional<UserRecord>> FindUserByUsername(
      const std::string& username) override;

  // Signups from `ip` since `since` — the per-IP daily cap check.
  absl::StatusOr<int64_t> CountRecentSignups(const std::string& ip,
                                             absl::Time since) override;

  absl::StatusOr<UserProfile> GetUser(int64_t user_id) override;

 private:
  Db& db_;
};

}  // namespace firefly

#endif  // FIREFLY_AUTH_USER_REPO_H_
