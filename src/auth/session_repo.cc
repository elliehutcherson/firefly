#include "src/auth/session_repo.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "src/db/db.h"
#include "src/db/row_reader.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

// The bytea text-input form of the stored hash ("\x" + 64 hex chars).
std::string ByteaLiteral(const std::string& sha256_hex) {
  return absl::StrCat("\\x", sha256_hex);
}

std::string FormatTimestamp(absl::Time t) {
  return absl::FormatTime(absl::RFC3339_full, t, absl::UTCTimeZone());
}

absl::Status InsertSession(SqlExecutor& executor,
                           const std::string& token_sha256_hex, int64_t user_id,
                           absl::Time now, absl::Time expires_at,
                           const std::optional<std::string>& ip) {
  return executor
      .Execute(
          "INSERT INTO sessions (token_hash, user_id, created_at, "
          "expires_at, last_seen_at, ip) VALUES ($1::bytea, $2, "
          "$3::timestamptz, $4::timestamptz, $3::timestamptz, $5::inet)",
          {ByteaLiteral(token_sha256_hex), absl::StrCat(user_id),
           FormatTimestamp(now), FormatTimestamp(expires_at), ip})
      .status();
}

}  // namespace

absl::Status SessionRepo::CreateSession(const std::string& token_sha256_hex,
                                        int64_t user_id, absl::Time now,
                                        absl::Time expires_at,
                                        const std::optional<std::string>& ip) {
  return InsertSession(db_, token_sha256_hex, user_id, now, expires_at, ip);
}

absl::Status SessionRepo::CreateSession(
    Transaction& transaction, const std::string& token_sha256_hex,
    int64_t user_id, absl::Time now, absl::Time expires_at,
    const std::optional<std::string>& ip) {
  return InsertSession(transaction, token_sha256_hex, user_id, now, expires_at,
                       ip);
}

absl::StatusOr<std::optional<SessionRecord>> SessionRepo::FindSession(
    const std::string& token_sha256_hex, absl::Time now) {
  // last_seen_at comes back as epoch seconds to avoid parsing Postgres'
  // timestamptz text form.
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT user_id, extract(epoch FROM last_seen_at)::bigint "
                 "FROM sessions WHERE token_hash = $1::bytea "
                 "AND expires_at > $2::timestamptz",
                 {ByteaLiteral(token_sha256_hex), FormatTimestamp(now)}));
  if (rows.empty()) {
    return std::nullopt;
  }
  const RowReader row(rows[0], "sessions");
  SessionRecord record;
  ASSIGN_OR_RETURN(record.user_id, row.Int64(0));
  ASSIGN_OR_RETURN(const int64_t epoch_seconds, row.Int64(1));
  record.last_seen_at = absl::FromUnixSeconds(epoch_seconds);
  return record;
}

absl::Status SessionRepo::TouchSession(const std::string& token_sha256_hex,
                                       absl::Time now,
                                       absl::Time new_expires_at) {
  return db_.Execute("UPDATE sessions SET last_seen_at = $2::timestamptz, "
                     "expires_at = $3::timestamptz WHERE token_hash = "
                     "$1::bytea",
                     {ByteaLiteral(token_sha256_hex), FormatTimestamp(now),
                      FormatTimestamp(new_expires_at)})
      .status();
}

absl::StatusOr<bool> SessionRepo::DeleteSession(
    const std::string& token_sha256_hex) {
  ASSIGN_OR_RETURN(const int64_t deleted,
                   db_.Execute("DELETE FROM sessions WHERE token_hash = "
                                "$1::bytea",
                                {ByteaLiteral(token_sha256_hex)}));
  return deleted > 0;
}

absl::StatusOr<int64_t> SessionRepo::DeleteExpiredSessions(absl::Time now) {
  return db_.Execute("DELETE FROM sessions WHERE expires_at < $1::timestamptz",
                      {FormatTimestamp(now)});
}

}  // namespace firefly
