#ifndef FIREFLY_AUTH_USER_STORE_H_
#define FIREFLY_AUTH_USER_STORE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/db/transaction.h"

namespace firefly {

struct UserRecord {
  int64_t id = 0;
  std::string username;
  std::string password_hash;
};

struct UserProfile {
  int64_t id = 0;
  std::string username;
  std::optional<std::string> email;
  int64_t cash_cents = 0;
};

// Auth's persistence boundary for user account data.
class UserStore {
 public:
  virtual ~UserStore() = default;

  virtual absl::StatusOr<int64_t> CreateUser(
      Transaction& transaction, const std::string& username,
      const std::string& password_hash,
      const std::optional<std::string>& email,
      const std::optional<std::string>& signup_ip) = 0;
  virtual absl::StatusOr<std::optional<UserRecord>> FindUserByUsername(
      const std::string& username) = 0;
  virtual absl::StatusOr<int64_t> CountRecentSignups(const std::string& ip,
                                                     absl::Time since) = 0;
  virtual absl::StatusOr<UserProfile> GetUser(int64_t user_id) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_AUTH_USER_STORE_H_
