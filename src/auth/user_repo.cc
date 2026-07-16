#include "src/auth/user_repo.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/db/db.h"
#include "src/db/row_reader.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

std::string FormatTimestamp(absl::Time t) {
  return absl::FormatTime(absl::RFC3339_full, t, absl::UTCTimeZone());
}

absl::StatusOr<int64_t> InsertUser(
    SqlExecutor& executor, const std::string& username,
    const std::string& password_hash, const std::optional<std::string>& email,
    const std::optional<std::string>& signup_ip) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      executor.Query("INSERT INTO users (username, password_hash, email, "
                     "signup_ip) VALUES ($1, $2, $3, $4::inet) RETURNING id",
                     {username, password_hash, email, signup_ip}));
  if (rows.size() != 1) {
    return absl::InternalError("INSERT ... RETURNING id returned no row");
  }
  return RowReader(rows[0], "users").Int64(0);
}

}  // namespace

absl::StatusOr<int64_t> UserRepo::CreateUser(
    const std::string& username, const std::string& password_hash,
    const std::optional<std::string>& email,
    const std::optional<std::string>& signup_ip) {
  return InsertUser(db_, username, password_hash, email, signup_ip);
}

absl::StatusOr<int64_t> UserRepo::CreateUser(
    Transaction& transaction, const std::string& username,
    const std::string& password_hash, const std::optional<std::string>& email,
    const std::optional<std::string>& signup_ip) {
  return InsertUser(transaction, username, password_hash, email, signup_ip);
}

absl::StatusOr<std::optional<UserRecord>> UserRepo::FindUserByUsername(
    const std::string& username) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT id, username, password_hash FROM users "
                 "WHERE lower(username) = lower($1)",
                 {username}));
  if (rows.empty()) {
    return std::nullopt;
  }
  const RowReader row(rows[0], "users");
  UserRecord record;
  ASSIGN_OR_RETURN(record.id, row.Int64(0));
  ASSIGN_OR_RETURN(const absl::string_view name, row.RequiredString(1));
  record.username = std::string(name);
  ASSIGN_OR_RETURN(const absl::string_view hash, row.RequiredString(2));
  record.password_hash = std::string(hash);
  return record;
}

absl::StatusOr<int64_t> UserRepo::CountRecentSignups(const std::string& ip,
                                                     absl::Time since) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT count(*) FROM users WHERE signup_ip = $1::inet "
                 "AND created_at > $2::timestamptz",
                 {ip, FormatTimestamp(since)}));
  if (rows.empty()) {
    return absl::InternalError("count(*) returned no row");
  }
  return RowReader(rows[0], "users").Int64(0);
}

absl::StatusOr<UserProfile> UserRepo::GetUser(int64_t user_id) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT id, username, email, cash_cents FROM users "
                 "WHERE id = $1",
                 {absl::StrCat(user_id)}));
  if (rows.empty()) {
    return absl::NotFoundError(absl::StrCat("no user ", user_id));
  }
  const RowReader row(rows[0], "users");
  UserProfile profile;
  ASSIGN_OR_RETURN(profile.id, row.Int64(0));
  ASSIGN_OR_RETURN(const absl::string_view name, row.RequiredString(1));
  profile.username = std::string(name);
  ASSIGN_OR_RETURN(const std::optional<absl::string_view> email,
                   row.OptionalString(2));
  if (email.has_value()) {
    profile.email = std::string(*email);
  }
  ASSIGN_OR_RETURN(profile.cash_cents, row.Int64(3));
  return profile;
}

}  // namespace firefly
