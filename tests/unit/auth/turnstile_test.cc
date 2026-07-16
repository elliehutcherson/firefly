#include "src/auth/turnstile.h"

#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/common/http.h"
#include "tests/fakes/common/fake_http_client.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Pair;

TEST(TurnstileTest, EmptySecretSkipsVerificationEntirely) {
  FakeHttpClient http;
  TurnstileVerifier verifier("", &http);

  EXPECT_FALSE(verifier.enabled());
  EXPECT_OK(verifier.Verify("any-token", "203.0.113.7"));
  EXPECT_TRUE(http.requests.empty());
}

TEST(TurnstileTest, PostsFormToSiteverify) {
  FakeHttpClient http;
  http.responses.push_back(HttpResponse{200, R"({"success": true})"});
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_OK(verifier.Verify("the-token", "203.0.113.7"));
  ASSERT_EQ(http.requests.size(), 1);
  EXPECT_EQ(http.methods[0], HttpMethod::kPost);
  EXPECT_EQ(http.requests[0].url,
            "https://challenges.cloudflare.com/turnstile/v0/siteverify");
  EXPECT_THAT(http.requests[0].form,
              ElementsAre(Pair("secret", "s3cret"),
                          Pair("response", "the-token"),
                          Pair("remoteip", "203.0.113.7")));
}

TEST(TurnstileTest, OmitsRemoteIpWhenUnknown) {
  FakeHttpClient http;
  http.responses.push_back(HttpResponse{200, R"({"success": true})"});
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_OK(verifier.Verify("the-token", std::nullopt));
  EXPECT_THAT(http.requests[0].form,
              ElementsAre(Pair("secret", "s3cret"),
                          Pair("response", "the-token")));
}

TEST(TurnstileTest, RejectionIsPermissionDenied) {
  FakeHttpClient http;
  http.responses.push_back(HttpResponse{
      200, R"({"success": false, "error-codes": ["invalid-input-response"]})"});
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_THAT(verifier.Verify("bad-token", std::nullopt),
              StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST(TurnstileTest, HttpErrorIsUnavailable) {
  FakeHttpClient http;
  http.responses.push_back(HttpResponse{500, "oops"});
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_THAT(verifier.Verify("token", std::nullopt),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(TurnstileTest, JunkBodyIsUnavailable) {
  FakeHttpClient http;
  http.responses.push_back(HttpResponse{200, "not json"});
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_THAT(verifier.Verify("token", std::nullopt),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(TurnstileTest, TransportErrorPropagates) {
  FakeHttpClient http;
  http.responses.push_back(absl::DeadlineExceededError("timed out"));
  TurnstileVerifier verifier("s3cret", &http);

  EXPECT_THAT(verifier.Verify("token", std::nullopt),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

}  // namespace
}  // namespace firefly
