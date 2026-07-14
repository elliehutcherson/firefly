#ifndef FIREFLY_COMMON_HTTP_CPR_H_
#define FIREFLY_COMMON_HTTP_CPR_H_

#include <functional>
#include <string>

#include "absl/status/statusor.h"
#include "src/common/http.h"

// CprHttpClient is split out of http.cc into its own header so tests can
// inject a fake transport without including cpr headers (see docs/STYLE.md:
// http.cc is the only file that includes cpr).

namespace firefly {

// The raw outcome of attempting the network call, before it's translated
// into an HttpResponse or a Status.
struct TransportResult {
  bool ok = false;
  bool timed_out = false;
  std::string error_message;
  int status_code = 0;
  std::string body;
};

// Exposed for testing the Status-mapping logic in isolation; production code
// should go through CreateHttpClient() instead.
class CprHttpClient : public HttpClient {
 public:
  struct Options {
    // Defaults to the real cpr-backed transport when left unset.
    std::function<TransportResult(const HttpRequest&)> transport;
  };

  explicit CprHttpClient(Options options = {});

  absl::StatusOr<HttpResponse> Get(const HttpRequest& request) override;

 private:
  std::function<TransportResult(const HttpRequest&)> transport_cb_;
};

}  // namespace firefly

#endif  // FIREFLY_COMMON_HTTP_CPR_H_
