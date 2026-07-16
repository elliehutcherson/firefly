#include "src/auth/session_repo.h"

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/fakes/db/fake_db.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::testing::HasSubstr;
using ::testing::StartsWith;

constexpr char kHash[] =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

TEST(SessionRepoTest, CreateSessionFramesByteaAndTimestamps) {
  FakeDb db;
  db.execute_results.push_back(1);
  SessionRepo repo(db);

  const absl::Time now = absl::FromUnixSeconds(1786000000);
  ASSERT_OK(repo.CreateSession(kHash, 42, now, now + absl::Hours(24 * 30),
                               "203.0.113.7"));
  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("$1::bytea"));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("$5::inet"));
  ASSERT_EQ(db.calls[0].params.size(), 5);
  ASSERT_TRUE(db.calls[0].params[0].has_value());
  EXPECT_THAT(*db.calls[0].params[0], StartsWith("\\x"));
  EXPECT_EQ(*db.calls[0].params[0], std::string("\\x") + kHash);
  EXPECT_EQ(*db.calls[0].params[1], "42");
}

TEST(SessionRepoTest, FindSessionParsesEpochAndChecksExpiry) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"42", "1786000000"}}});
  SessionRepo repo(db);

  absl::StatusOr<std::optional<SessionRecord>> session =
      repo.FindSession(kHash, absl::FromUnixSeconds(1786000500));
  ASSERT_OK(session);
  ASSERT_TRUE(session->has_value());
  EXPECT_EQ((*session)->user_id, 42);
  EXPECT_EQ((*session)->last_seen_at, absl::FromUnixSeconds(1786000000));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("expires_at > $2::timestamptz"));
}

TEST(SessionRepoTest, FindSessionMissingIsNullopt) {
  FakeDb db;
  db.query_results.push_back(Rows{});
  SessionRepo repo(db);

  absl::StatusOr<std::optional<SessionRecord>> session =
      repo.FindSession(kHash, absl::UnixEpoch());
  ASSERT_OK(session);
  EXPECT_FALSE(session->has_value());
}

TEST(SessionRepoTest, TouchUpdatesBothTimestamps) {
  FakeDb db;
  db.execute_results.push_back(1);
  SessionRepo repo(db);

  const absl::Time now = absl::FromUnixSeconds(1786000000);
  ASSERT_OK(repo.TouchSession(kHash, now, now + absl::Hours(24 * 30)));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("SET last_seen_at = $2::timestamptz"));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("expires_at = $3::timestamptz"));
}

TEST(SessionRepoTest, DeleteSessionReportsWhetherARowExisted) {
  FakeDb db;
  db.execute_results.push_back(1);
  db.execute_results.push_back(0);
  SessionRepo repo(db);

  absl::StatusOr<bool> first = repo.DeleteSession(kHash);
  ASSERT_OK(first);
  EXPECT_TRUE(*first);
  absl::StatusOr<bool> second = repo.DeleteSession(kHash);
  ASSERT_OK(second);
  EXPECT_FALSE(*second);
}

TEST(SessionRepoTest, DeleteExpiredReturnsCount) {
  FakeDb db;
  db.execute_results.push_back(3);
  SessionRepo repo(db);

  absl::StatusOr<int64_t> deleted =
      repo.DeleteExpiredSessions(absl::FromUnixSeconds(1786000000));
  ASSERT_OK(deleted);
  EXPECT_EQ(*deleted, 3);
  EXPECT_THAT(db.calls[0].sql,
              HasSubstr("DELETE FROM sessions WHERE expires_at < "
                        "$1::timestamptz"));
}

}  // namespace
}  // namespace firefly
