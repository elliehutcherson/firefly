#ifndef FIREFLY_COMMON_HTTP_H_
#define FIREFLY_COMMON_HTTP_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

// Thin boundary around outbound HTTP (Alpaca, Turnstile). Per the wrapper
// policy in docs/STYLE.md, http.cc is the only code that includes cpr
// headers, and this interface is the single place to fake in tests.

namespace firefly {

struct HttpRequest {
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::vector<std::pair<std::string, std::string>> query_params;
  // POST only: sent URL-encoded as application/x-www-form-urlencoded.
  std::vector<std::pair<std::string, std::string>> form;
  // POST only: raw request body, sent as application/json. When set, `form`
  // is ignored.
  std::string body;
};

struct HttpResponse {
  int status_code = 0;
  std::string body;
  // Cookies the response set, as (name, value) — how a client carries a
  // server session (e2e tests drive the real login flow through this).
  std::vector<std::pair<std::string, std::string>> cookies;
};

enum class HttpMethod : std::uint8_t { kGet, kPost };

class HttpClient {
 public:
  virtual ~HttpClient() = default;

  // Transport failures (DNS, refused connection, timeout) come back as an
  // error Status; an HTTP error is a success whose status_code says so —
  // what a 429 means is the caller's business, not the transport's.
  virtual absl::StatusOr<HttpResponse> Get(const HttpRequest& request) = 0;

  // Form-encoded POST (request.form); same error contract as Get.
  virtual absl::StatusOr<HttpResponse> Post(const HttpRequest& request) = 0;
};

// The production client, backed by cpr/libcurl.
std::unique_ptr<HttpClient> CreateHttpClient();

}  // namespace firefly

#endif  // FIREFLY_COMMON_HTTP_H_
