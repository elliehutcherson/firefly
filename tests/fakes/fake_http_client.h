#ifndef FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_
#define FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_

#include <deque>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/common/http.h"

namespace firefly {

// Replays canned responses and records every request it receives.
class FakeHttpClient : public HttpClient {
 public:
  absl::StatusOr<HttpResponse> Get(const HttpRequest& request) override {
    requests.push_back(request);
    if (responses.empty()) {
      return absl::InternalError("FakeHttpClient: no canned response left");
    }
    absl::StatusOr<HttpResponse> response = std::move(responses.front());
    responses.pop_front();
    return response;
  }

  std::vector<HttpRequest> requests;
  std::deque<absl::StatusOr<HttpResponse>> responses;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_
