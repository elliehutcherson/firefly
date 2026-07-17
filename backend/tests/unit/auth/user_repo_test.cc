#include "auth/user_repo.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "fakes/db/fake_db.h"
#include "support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

TEST(UserRepoTest, CreateUserBindsAllParamsAndReturnsId) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"42"}}});
  UserRepo repo(db);
  absl::StatusOr<std::unique_ptr<Transaction>> transaction = db.Begin();
  ASSERT_OK(transaction);

  absl::StatusOr<int64_t> id =
      repo.CreateUser(**transaction, "ellie", "$argon2id$fake",
                      "e@example.com", "203.0.113.7");
  ASSERT_OK(id);
  EXPECT_EQ(*id, 42);
  ASSERT_EQ(db.calls.size(), 1);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("$4::inet"));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("RETURNING id"));
  EXPECT_THAT(db.calls[0].params,
              ElementsAre("ellie", "$argon2id$fake", "e@example.com",
                          "203.0.113.7"));
}

TEST(UserRepoTest, CreateUserBindsNullEmailAndIp) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"7"}}});
  UserRepo repo(db);
  absl::StatusOr<std::unique_ptr<Transaction>> transaction = db.Begin();
  ASSERT_OK(transaction);

  ASSERT_OK(repo.CreateUser(**transaction, "ellie", "hash", std::nullopt,
                            std::nullopt));
  EXPECT_THAT(db.calls[0].params,
              ElementsAre("ellie", "hash", std::nullopt, std::nullopt));
}

TEST(UserRepoTest, CreateUserPropagatesAlreadyExists) {
  FakeDb db;
  db.query_results.push_back(absl::AlreadyExistsError("dup [sqlstate 23505]"));
  UserRepo repo(db);
  absl::StatusOr<std::unique_ptr<Transaction>> transaction = db.Begin();
  ASSERT_OK(transaction);

  EXPECT_THAT(repo.CreateUser(**transaction, "ellie", "hash", std::nullopt,
                              std::nullopt),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(UserRepoTest, FindUserByUsernameIsCaseInsensitiveLookup) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"42", "Ellie", "$argon2id$x"}}});
  UserRepo repo(db);

  absl::StatusOr<std::optional<UserRecord>> user =
      repo.FindUserByUsername("ELLIE");
  ASSERT_OK(user);
  ASSERT_TRUE(user->has_value());
  EXPECT_EQ((*user)->id, 42);
  EXPECT_EQ((*user)->username, "Ellie");
  EXPECT_EQ((*user)->password_hash, "$argon2id$x");
  EXPECT_THAT(db.calls[0].sql, HasSubstr("lower(username) = lower($1)"));
}

TEST(UserRepoTest, FindUserMissingIsNulloptNotError) {
  FakeDb db;
  db.query_results.push_back(Rows{});
  UserRepo repo(db);

  absl::StatusOr<std::optional<UserRecord>> user =
      repo.FindUserByUsername("ghost");
  ASSERT_OK(user);
  EXPECT_FALSE(user->has_value());
}

TEST(UserRepoTest, CountRecentSignupsBindsIpAndSince) {
  FakeDb db;
  db.query_results.push_back(Rows{Row{{"2"}}});
  UserRepo repo(db);

  absl::StatusOr<int64_t> count = repo.CountRecentSignups(
      "203.0.113.7", absl::FromUnixSeconds(1786000000));
  ASSERT_OK(count);
  EXPECT_EQ(*count, 2);
  EXPECT_THAT(db.calls[0].sql, HasSubstr("signup_ip = $1::inet"));
  EXPECT_THAT(db.calls[0].sql, HasSubstr("created_at > $2::timestamptz"));
  ASSERT_EQ(db.calls[0].params.size(), 2);
  EXPECT_EQ(db.calls[0].params[0], "203.0.113.7");
}

TEST(UserRepoTest, GetUserMapsProfileAndNullEmail) {
  FakeDb db;
  db.query_results.push_back(
      Rows{Row{{"42", "ellie", std::nullopt, "1000000"}}});
  UserRepo repo(db);

  absl::StatusOr<UserProfile> profile = repo.GetUser(42);
  ASSERT_OK(profile);
  EXPECT_EQ(profile->id, 42);
  EXPECT_EQ(profile->username, "ellie");
  EXPECT_FALSE(profile->email.has_value());
  EXPECT_EQ(profile->cash_cents, 1000000);
}

TEST(UserRepoTest, GetUserMissingIsNotFound) {
  FakeDb db;
  db.query_results.push_back(Rows{});
  UserRepo repo(db);

  EXPECT_THAT(repo.GetUser(999), StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace firefly
