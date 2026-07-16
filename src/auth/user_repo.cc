#include "src/auth/user_repo.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/common/db.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

absl::StatusOr<absl::string_view> GetColumn(const Row& row, size_t index) {
  if (index >= row.columns.size() || !row.columns[index].has_value()) {
    return absl::InternalError(
        absl::StrCat("users row missing column ", index));
  }
  return absl::string_view(*row.columns[index]);
}

absl::StatusOr<int64_t> GetInt64(const Row& row, size_t index) {
  ASSIGN_OR_RETURN(const absl::string_view text, GetColumn(row, index));
  int64_t value = 0;
  if (!absl::SimpleAtoi(text, &value)) {
    return absl::InternalError(absl::StrCat("bad integer from db: '", text, "'"));
  }
  return value;
}

std::string FormatTimestamp(absl::Time t) {
  return absl::FormatTime(absl::RFC3339_full, t, absl::UTCTimeZone());
}

}  // namespace

absl::StatusOr<int64_t> UserRepo::CreateUser(
    const std::string& username, const std::string& password_hash,
    const std::optional<std::string>& email,
    const std::optional<std::string>& signup_ip) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("INSERT INTO users (username, password_hash, email, "
                 "signup_ip) VALUES ($1, $2, $3, $4::inet) RETURNING id",
                 {username, password_hash, email, signup_ip}));
  if (rows.size() != 1) {
    return absl::InternalError("INSERT ... RETURNING id returned no row");
  }
  return GetInt64(rows[0], 0);
}

absl::StatusOr<std::optional<UserRecord>> UserRepo::FindUserByUsername(
    const std::string& username) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT id, username, password_hash FROM users "
                 "WHERE lower(username) = lower($1)",
                 {username}));
  if (rows.empty()) {
    return std::nullopt;
  }
  UserRecord record;
  ASSIGN_OR_RETURN(record.id, GetInt64(rows[0], 0));
  ASSIGN_OR_RETURN(const absl::string_view name, GetColumn(rows[0], 1));
  record.username = std::string(name);
  ASSIGN_OR_RETURN(const absl::string_view hash, GetColumn(rows[0], 2));
  record.password_hash = std::string(hash);
  return record;
}

absl::StatusOr<int64_t> UserRepo::CountRecentSignups(const std::string& ip,
                                                     absl::Time since) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT count(*) FROM users WHERE signup_ip = $1::inet "
                 "AND created_at > $2::timestamptz",
                 {ip, FormatTimestamp(since)}));
  if (rows.empty()) {
    return absl::InternalError("count(*) returned no row");
  }
  return GetInt64(rows[0], 0);
}

absl::StatusOr<UserProfile> UserRepo::GetUser(int64_t user_id) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT id, username, email, cash_cents FROM users "
                 "WHERE id = $1",
                 {absl::StrCat(user_id)}));
  if (rows.empty()) {
    return absl::NotFoundError(absl::StrCat("no user ", user_id));
  }
  UserProfile profile;
  ASSIGN_OR_RETURN(profile.id, GetInt64(rows[0], 0));
  ASSIGN_OR_RETURN(const absl::string_view name, GetColumn(rows[0], 1));
  profile.username = std::string(name);
  // email is nullable; keep nullopt for NULL.
  if (rows[0].columns.size() > 2 && rows[0].columns[2].has_value()) {
    profile.email = *rows[0].columns[2];
  }
  ASSIGN_OR_RETURN(profile.cash_cents, GetInt64(rows[0], 3));
  return profile;
}

}  // namespace firefly
