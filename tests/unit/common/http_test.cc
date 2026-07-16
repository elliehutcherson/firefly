#include "src/common/http_cpr.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/common/http.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

TEST(CprHttpClientTest, SuccessPassesThroughStatusCodeAndBody) {
  CprHttpClient client(CprHttpClient::Options{
      .transport = [](const HttpRequest&, HttpMethod) {
        return TransportResult{.ok = true, .status_code = 201, .body = "hi"};
      }});

  absl::StatusOr<HttpResponse> response =
      client.Get(HttpRequest{.url = "http://example.test/"});
  ASSERT_OK(response);
  EXPECT_EQ(response->status_code, 201);
  EXPECT_EQ(response->body, "hi");
}

TEST(CprHttpClientTest, HttpErrorStatusIsNotATransportError) {
  CprHttpClient client(CprHttpClient::Options{
      .transport = [](const HttpRequest&, HttpMethod) {
        return TransportResult{.ok = true, .status_code = 404, .body = "nope"};
      }});

  absl::StatusOr<HttpResponse> response =
      client.Get(HttpRequest{.url = "http://example.test/"});
  ASSERT_OK(response);
  EXPECT_EQ(response->status_code, 404);
}

TEST(CprHttpClientTest, TimeoutIsDeadlineExceeded) {
  CprHttpClient client(CprHttpClient::Options{
      .transport = [](const HttpRequest&, HttpMethod) {
        return TransportResult{.timed_out = true};
      }});

  EXPECT_THAT(client.Get(HttpRequest{.url = "http://example.test/"}),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST(CprHttpClientTest, TransportFailureIsUnavailable) {
  CprHttpClient client(CprHttpClient::Options{
      .transport = [](const HttpRequest&, HttpMethod) {
        return TransportResult{.ok = false,
                                .error_message = "connection refused"};
      }});

  EXPECT_THAT(client.Get(HttpRequest{.url = "http://example.test/"}),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(CprHttpClientTest, PassesRequestThroughToTransport) {
  HttpRequest seen;
  CprHttpClient client(CprHttpClient::Options{
      .transport = [&](const HttpRequest& request, HttpMethod) {
        seen = request;
        return TransportResult{.ok = true, .status_code = 200};
      }});

  ASSERT_OK(client.Get(HttpRequest{.url = "http://example.test/path",
                                   .headers = {{"X-Test", "1"}},
                                   .query_params = {{"q", "v"}}}));
  EXPECT_EQ(seen.url, "http://example.test/path");
  ASSERT_EQ(seen.headers.size(), 1);
  EXPECT_EQ(seen.headers[0].first, "X-Test");
  ASSERT_EQ(seen.query_params.size(), 1);
  EXPECT_EQ(seen.query_params[0].second, "v");
}

TEST(CprHttpClientTest, PostSendsFormAndMethodToTransport) {
  HttpRequest seen;
  HttpMethod seen_method = HttpMethod::kGet;
  CprHttpClient client(CprHttpClient::Options{
      .transport = [&](const HttpRequest& request, HttpMethod method) {
        seen = request;
        seen_method = method;
        return TransportResult{.ok = true, .status_code = 200, .body = "ok"};
      }});

  absl::StatusOr<HttpResponse> response =
      client.Post(HttpRequest{.url = "http://example.test/verify",
                              .form = {{"secret", "s3"}, {"response", "tok"}}});
  ASSERT_OK(response);
  EXPECT_EQ(seen_method, HttpMethod::kPost);
  ASSERT_EQ(seen.form.size(), 2);
  EXPECT_EQ(seen.form[0].first, "secret");
  EXPECT_EQ(seen.form[1].second, "tok");
}

TEST(CprHttpClientTest, PostTimeoutIsDeadlineExceeded) {
  CprHttpClient client(CprHttpClient::Options{
      .transport = [](const HttpRequest&, HttpMethod) {
        return TransportResult{.timed_out = true};
      }});

  EXPECT_THAT(client.Post(HttpRequest{.url = "http://example.test/"}),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST(CprHttpClientTest, GetStaysGetThroughTransport) {
  HttpMethod seen_method = HttpMethod::kPost;
  CprHttpClient client(CprHttpClient::Options{
      .transport = [&](const HttpRequest&, HttpMethod method) {
        seen_method = method;
        return TransportResult{.ok = true, .status_code = 200};
      }});

  ASSERT_OK(client.Get(HttpRequest{.url = "http://example.test/"}));
  EXPECT_EQ(seen_method, HttpMethod::kGet);
}

TEST(CreateHttpClientTest, ReturnsAClient) {
  EXPECT_NE(CreateHttpClient(), nullptr);
}

}  // namespace
}  // namespace firefly
