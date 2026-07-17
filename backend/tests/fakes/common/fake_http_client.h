#ifndef FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_
#define FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_

#include <deque>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/http.h"

namespace firefly {

// Replays canned responses and records every request it receives; `methods`
// runs parallel to `requests`.
class FakeHttpClient : public HttpClient {
 public:
  absl::StatusOr<HttpResponse> Get(const HttpRequest& request) override {
    return Record(request, HttpMethod::kGet);
  }

  absl::StatusOr<HttpResponse> Post(const HttpRequest& request) override {
    return Record(request, HttpMethod::kPost);
  }

  std::vector<HttpRequest> requests;
  std::vector<HttpMethod> methods;
  std::deque<absl::StatusOr<HttpResponse>> responses;

 private:
  absl::StatusOr<HttpResponse> Record(const HttpRequest& request,
                                      HttpMethod method) {
    requests.push_back(request);
    methods.push_back(method);
    if (responses.empty()) {
      return absl::InternalError("FakeHttpClient: no canned response left");
    }
    absl::StatusOr<HttpResponse> response = std::move(responses.front());
    responses.pop_front();
    return response;
  }
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_HTTP_CLIENT_H_
