#include "src/auth/session_auth.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/auth/crypto.h"
#include "src/auth/session_store.h"
#include "tests/fakes/auth/fake_session_store.h"
#include "tests/fakes/common/fake_clock.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

constexpr char kToken[] = "deadbeef";

class SessionAuthTest : public ::testing::Test {
 protected:
  SessionAuth Auth() {
    return {.sessions = sessions_,
            .clock = clock_,
            .session_ttl = absl::Hours(24 * 30)};
  }

  // A live session for kToken, last seen `seen_ago` before now.
  void AddSession(absl::Duration seen_ago) {
    sessions_.sessions[Sha256Hex(kToken)] = {
        .user_id = 42,
        .last_seen_at = clock_.Now() - seen_ago,
        .expires_at = clock_.Now() + absl::Hours(24)};
  }

  FakeSessionStore sessions_;
  FakeClock clock_{absl::FromUnixSeconds(1786000000)};
};

TEST_F(SessionAuthTest, EmptyTokenIsUnauthenticatedWithoutAStoreCall) {
  EXPECT_THAT(RequireSession(Auth(), ""),
              StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_EQ(sessions_.finds, 0);
}

TEST_F(SessionAuthTest, UnknownTokenIsUnauthenticated) {
  EXPECT_THAT(RequireSession(Auth(), kToken),
              StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_EQ(sessions_.finds, 1);
}

TEST_F(SessionAuthTest, ExpiredSessionIsUnauthenticated) {
  AddSession(absl::Minutes(1));
  sessions_.sessions[Sha256Hex(kToken)].expires_at =
      clock_.Now() - absl::Seconds(1);
  EXPECT_THAT(RequireSession(Auth(), kToken),
              StatusIs(absl::StatusCode::kUnauthenticated));
}

TEST_F(SessionAuthTest, LooksUpTheTokenHashNotTheToken) {
  AddSession(absl::Minutes(1));
  ASSERT_OK(RequireSession(Auth(), kToken));
  // The raw token is never a store key.
  EXPECT_FALSE(sessions_.sessions.contains(kToken));
}

TEST_F(SessionAuthTest, FreshSessionIsReturnedWithoutATouch) {
  AddSession(absl::Minutes(59));
  absl::StatusOr<SessionRecord> session = RequireSession(Auth(), kToken);
  ASSERT_OK(session);
  EXPECT_EQ(session->user_id, 42);
  EXPECT_EQ(sessions_.touches, 0);
}

TEST_F(SessionAuthTest, StaleSessionIsTouchedWithTheSlidingTtl) {
  AddSession(absl::Minutes(61));
  ASSERT_OK(RequireSession(Auth(), kToken));
  EXPECT_EQ(sessions_.touches, 1);
  const FakeSessionStore::StoredSession& stored =
      sessions_.sessions[Sha256Hex(kToken)];
  EXPECT_EQ(stored.last_seen_at, clock_.Now());
  EXPECT_EQ(stored.expires_at, clock_.Now() + absl::Hours(24 * 30));
}

TEST_F(SessionAuthTest, StoreErrorsPropagate) {
  sessions_.outage = absl::UnavailableError("db down");
  EXPECT_THAT(RequireSession(Auth(), kToken),
              StatusIs(absl::StatusCode::kUnavailable));

  sessions_.outage = absl::OkStatus();
  AddSession(absl::Hours(2));
  sessions_.touch_status = absl::UnavailableError("touch failed");
  EXPECT_THAT(RequireSession(Auth(), kToken),
              StatusIs(absl::StatusCode::kUnavailable));
}

}  // namespace
}  // namespace firefly
