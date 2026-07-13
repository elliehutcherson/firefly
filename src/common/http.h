#ifndef FIREFLY_COMMON_HTTP_H_
#define FIREFLY_COMMON_HTTP_H_

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
};

struct HttpResponse {
  int status_code = 0;
  std::string body;
};

class HttpClient {
 public:
  virtual ~HttpClient() = default;

  // Transport failures (DNS, refused connection, timeout) come back as an
  // error Status; an HTTP error is a success whose status_code says so —
  // what a 429 means is the caller's business, not the transport's.
  virtual absl::StatusOr<HttpResponse> Get(const HttpRequest& request) = 0;
};

// The production client, backed by cpr/libcurl.
std::unique_ptr<HttpClient> CreateHttpClient();

}  // namespace firefly

#endif  // FIREFLY_COMMON_HTTP_H_
