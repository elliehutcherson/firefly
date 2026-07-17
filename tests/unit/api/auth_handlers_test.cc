#include "api/auth_handlers.h"

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "auth/crypto.h"
#include "auth/session_repo.h"
#include "auth/turnstile.h"
#include "auth/user_repo.h"
#include "db/db.h"
#include "fakes/common/fake_clock.h"
#include "fakes/db/fake_db.h"
#include "fakes/common/fake_http_client.h"
#include "support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;
using ::testing::StartsWith;

constexpr char kIp[] = "203.0.113.7";

// Real repos and verifier over fakes: the cores are exercised with the same
// objects production uses; only Db/Http/Clock are faked.
class AuthHandlersTest : public ::testing::Test {
 protected:
  AuthDeps Deps(TurnstileVerifier& turnstile) {
    return {.db = db_,
            .users = users_,
            .sessions = sessions_,
            .turnstile = turnstile,
            .clock = clock_,
            .session_ttl = absl::Hours(24 * 30),
            .signup_ip_daily_cap = 3};
  }

  AuthDeps Deps() { return Deps(turnstile_); }

  // Signup runs: count recent signups (when ip present), insert user
  // RETURNING id, insert session.
  void CannedSignupDb(int64_t recent, int64_t new_id) {
    db_.query_results.push_back(Rows{Row{{absl::StrCat(recent)}}});
    db_.query_results.push_back(Rows{Row{{absl::StrCat(new_id)}}});
    db_.execute_results.push_back(1);
  }

  FakeDb db_;
  UserRepo users_{db_};
  SessionRepo sessions_{db_};
  FakeHttpClient http_;
  TurnstileVerifier turnstile_{"", http_};  // Disabled by default.
  FakeClock clock_{absl::FromUnixSeconds(1786000000)};
};

TEST(NormalizeUsernameTest, AcceptsAndPreservesCase) {
  EXPECT_THAT(NormalizeUsername("Ellie_42"), IsOkAndHolds("Ellie_42"));
  EXPECT_THAT(NormalizeUsername("abc"), IsOkAndHolds("abc"));
}

TEST(NormalizeUsernameTest, RejectsJunk) {
  for (const char* junk : {"", "ab", "this_name_is_way_too_long", "el lie",
                           "ellie!", "ellie'; DROP", "тест", "a@b"}) {
    EXPECT_THAT(NormalizeUsername(junk),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "input: '" << junk << "'";
  }
}

TEST(SanitizeClientIpTest, AcceptsIpShapes) {
  EXPECT_EQ(SanitizeClientIp("203.0.113.7"), "203.0.113.7");
  EXPECT_EQ(SanitizeClientIp("2001:db8::1"), "2001:db8::1");
}

TEST(SanitizeClientIpTest, RejectsJunk) {
  EXPECT_EQ(SanitizeClientIp(""), std::nullopt);
  EXPECT_EQ(SanitizeClientIp("evil, 1.2.3.4"), std::nullopt);
  EXPECT_EQ(SanitizeClientIp("1.2.3.4'; DROP"), std::nullopt);
  EXPECT_EQ(SanitizeClientIp(std::string(64, '1')), std::nullopt);
}

TEST_F(AuthHandlersTest, SignupHappyPathStoresHashNotToken) {
  CannedSignupDb(/*recent=*/0, /*new_id=*/42);

  absl::StatusOr<AuthResult> result = Signup(
      Deps(), R"({"username":"ellie","password":"hunter2222"})", kIp);
  ASSERT_OK(result);
  EXPECT_EQ(result->body["user_id"], 42);
  EXPECT_EQ(result->body["username"], "ellie");
  EXPECT_THAT(result->set_session_token, MatchesRegex("[0-9a-f]{64}"));

  // users insert: password stored as argon2id, never plaintext.
  ASSERT_EQ(db_.calls.size(), 3);
  const DbParams& user_params = db_.calls[1].params;
  ASSERT_TRUE(user_params[1].has_value());
  EXPECT_THAT(*user_params[1], StartsWith("$argon2id$"));
  EXPECT_EQ(user_params[3], kIp);
  // sessions insert: stored value is \x + sha256(token), not the token.
  const DbParams& session_params = db_.calls[2].params;
  ASSERT_TRUE(session_params[0].has_value());
  EXPECT_EQ(*session_params[0],
            std::string("\\x") + Sha256Hex(result->set_session_token));
  EXPECT_EQ(db_.transaction_begins, 1);
  EXPECT_EQ(db_.transaction_commits, 1);
}

TEST_F(AuthHandlersTest, SignupWithoutIpSkipsTheCapQuery) {
  db_.query_results.push_back(Rows{Row{{"42"}}});  // Insert user only.
  db_.execute_results.push_back(1);

  ASSERT_OK(Signup(Deps(), R"({"username":"ellie","password":"hunter2222"})",
                   std::nullopt));
  EXPECT_THAT(db_.calls[0].sql, HasSubstr("INSERT INTO users"));
}

TEST_F(AuthHandlersTest, SignupPastCapIs429) {
  db_.query_results.push_back(Rows{Row{{"3"}}});  // Cap is 3.

  EXPECT_THAT(Signup(Deps(), R"({"username":"ellie","password":"hunter2222"})",
                     kIp),
              StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_EQ(db_.calls.size(), 1);  // Never reached the insert.
}

TEST_F(AuthHandlersTest, SignupDuplicateUsernameIs409) {
  db_.query_results.push_back(Rows{Row{{"0"}}});
  db_.query_results.push_back(
      absl::AlreadyExistsError("dup [sqlstate 23505]"));

  EXPECT_THAT(Signup(Deps(), R"({"username":"ellie","password":"hunter2222"})",
                     kIp),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_EQ(db_.transaction_begins, 1);
  EXPECT_EQ(db_.transaction_commits, 0);
}

TEST_F(AuthHandlersTest, SignupSessionFailureDoesNotCommitUser) {
  db_.query_results.push_back(Rows{Row{{"0"}}});
  db_.query_results.push_back(Rows{Row{{"42"}}});
  db_.execute_results.push_back(absl::UnavailableError("session insert failed"));

  EXPECT_THAT(Signup(Deps(), R"({"username":"ellie","password":"hunter2222"})",
                     kIp),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_EQ(db_.transaction_begins, 1);
  EXPECT_EQ(db_.transaction_commits, 0);
}

TEST_F(AuthHandlersTest, SignupValidationRejects) {
  const char* bad_bodies[] = {
      "not json",
      "[]",
      R"({"username":"ellie"})",                          // No password.
      R"({"username":"el","password":"hunter2222"})",     // Short username.
      R"({"username":"ellie","password":"short"})",       // Short password.
      R"({"username":"ellie","password":"hunter2222","email":"nope"})",
  };
  for (const char* body : bad_bodies) {
    EXPECT_THAT(Signup(Deps(), body, kIp),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "body: " << body;
  }
  EXPECT_TRUE(db_.calls.empty());
}

TEST_F(AuthHandlersTest, SignupTurnstileRejectionIs403) {
  TurnstileVerifier enabled("s3cret", http_);
  http_.responses.push_back(HttpResponse{200, R"({"success": false})"});
  AuthDeps deps = Deps(enabled);

  EXPECT_THAT(
      Signup(deps,
             R"({"username":"ellie","password":"hunter2222",
                 "turnstile_token":"tok"})",
             kIp),
      StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_TRUE(db_.calls.empty());  // Rejected before any DB work.
}

TEST_F(AuthHandlersTest, LoginHappyPath) {
  const std::string hash = *HashPassword("hunter2222");
  db_.query_results.push_back(Rows{Row{{"42", "ellie", hash}}});
  db_.execute_results.push_back(1);  // Session insert.

  absl::StatusOr<AuthResult> result =
      Login(Deps(), R"({"username":"ELLIE","password":"hunter2222"})", kIp);
  ASSERT_OK(result);
  EXPECT_EQ(result->body["user_id"], 42);
  EXPECT_THAT(result->set_session_token, MatchesRegex("[0-9a-f]{64}"));
}

TEST_F(AuthHandlersTest, LoginFailuresAreUniform401) {
  // Unknown user.
  db_.query_results.push_back(Rows{});
  absl::Status unknown =
      Login(Deps(), R"({"username":"ghost","password":"hunter2222"})", kIp)
          .status();
  // Wrong password.
  const std::string hash = *HashPassword("correct-horse");
  db_.query_results.push_back(Rows{Row{{"42", "ellie", hash}}});
  absl::Status wrong =
      Login(Deps(), R"({"username":"ellie","password":"hunter2222"})", kIp)
          .status();

  EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_THAT(wrong, StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_EQ(unknown.message(), wrong.message());
}

TEST_F(AuthHandlersTest, LogoutDeletesAndClears) {
  db_.execute_results.push_back(1);

  absl::StatusOr<AuthResult> result = Logout(Deps(), "deadbeef");
  ASSERT_OK(result);
  EXPECT_TRUE(result->clear_session);
  EXPECT_TRUE(result->set_session_token.empty());
  EXPECT_THAT(db_.calls[0].sql, HasSubstr("DELETE FROM sessions"));
}

TEST_F(AuthHandlersTest, LogoutWithoutCookieStillSucceeds) {
  absl::StatusOr<AuthResult> result = Logout(Deps(), "");
  ASSERT_OK(result);
  EXPECT_TRUE(result->clear_session);
  EXPECT_TRUE(db_.calls.empty());
}

TEST_F(AuthHandlersTest, MeRequiresASession) {
  EXPECT_THAT(GetMeJson(Deps(), ""),
              StatusIs(absl::StatusCode::kUnauthenticated));

  db_.query_results.push_back(Rows{});  // No session row.
  EXPECT_THAT(GetMeJson(Deps(), "deadbeef"),
              StatusIs(absl::StatusCode::kUnauthenticated));
}

TEST_F(AuthHandlersTest, MeReturnsProfileWithoutTouchingFreshSession) {
  const int64_t now = absl::ToUnixSeconds(clock_.Now());
  db_.query_results.push_back(
      Rows{Row{{"42", absl::StrCat(now - 60)}}});  // Seen a minute ago.
  db_.query_results.push_back(
      Rows{Row{{"42", "ellie", "e@example.com", "1000000"}}});

  absl::StatusOr<nlohmann::json> me = GetMeJson(Deps(), "deadbeef");
  ASSERT_OK(me);
  EXPECT_EQ((*me)["user_id"], 42);
  EXPECT_EQ((*me)["email"], "e@example.com");
  EXPECT_EQ((*me)["cash_cents"], 1000000);
  // Two queries, no UPDATE: fresh sessions are not touched.
  EXPECT_EQ(db_.calls.size(), 2);
}

TEST_F(AuthHandlersTest, MeTouchesStaleSession) {
  const int64_t now = absl::ToUnixSeconds(clock_.Now());
  db_.query_results.push_back(
      Rows{Row{{"42", absl::StrCat(now - 7200)}}});  // Seen 2h ago.
  db_.execute_results.push_back(1);                  // The touch.
  db_.query_results.push_back(
      Rows{Row{{"42", "ellie", std::nullopt, "1000000"}}});

  absl::StatusOr<nlohmann::json> me = GetMeJson(Deps(), "deadbeef");
  ASSERT_OK(me);
  EXPECT_TRUE((*me)["email"].is_null());
  ASSERT_EQ(db_.calls.size(), 3);
  EXPECT_THAT(db_.calls[1].sql, HasSubstr("UPDATE sessions"));
}

}  // namespace
}  // namespace firefly
