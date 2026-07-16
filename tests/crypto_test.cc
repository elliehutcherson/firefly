#include "src/auth/crypto.h"

#include <string>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/status_matchers.h"

namespace firefly {
namespace {

using ::testing::MatchesRegex;
using ::testing::StartsWith;

TEST(CryptoTest, InitSucceeds) { EXPECT_OK(InitCrypto()); }

TEST(CryptoTest, HashPasswordRoundTrips) {
  absl::StatusOr<std::string> hash = HashPassword("hunter2222");
  ASSERT_OK(hash);
  EXPECT_THAT(*hash, StartsWith("$argon2id$"));
  EXPECT_TRUE(VerifyPassword(*hash, "hunter2222"));
  EXPECT_FALSE(VerifyPassword(*hash, "hunter2223"));
  EXPECT_FALSE(VerifyPassword(*hash, ""));
}

TEST(CryptoTest, HashesAreSalted) {
  absl::StatusOr<std::string> first = HashPassword("same password");
  absl::StatusOr<std::string> second = HashPassword("same password");
  ASSERT_OK(first);
  ASSERT_OK(second);
  EXPECT_NE(*first, *second);
}

TEST(CryptoTest, VerifyRejectsGarbageHash) {
  EXPECT_FALSE(VerifyPassword("not-an-argon2-string", "pw"));
  EXPECT_FALSE(VerifyPassword("", "pw"));
}

TEST(CryptoTest, SessionTokenIs64LowercaseHexAndUnique) {
  const std::string token = GenerateSessionToken();
  EXPECT_THAT(token, MatchesRegex("[0-9a-f]{64}"));
  EXPECT_NE(token, GenerateSessionToken());
}

TEST(CryptoTest, Sha256HexMatchesKnownVector) {
  // FIPS 180-2 test vector for "abc".
  EXPECT_EQ(Sha256Hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Empty string vector.
  EXPECT_EQ(Sha256Hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

}  // namespace
}  // namespace firefly
