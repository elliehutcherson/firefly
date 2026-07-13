#include "src/common/http.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpr/cpr.h"  // IWYU pragma: keep

namespace firefly {
namespace {

constexpr int kTimeoutMs = 10000;

class CprHttpClient : public HttpClient {
 public:
  absl::StatusOr<HttpResponse> Get(const HttpRequest& request) override {
    cpr::Header header;
    for (const auto& [name, value] : request.headers) {
      header.emplace(name, value);
    }
    cpr::Parameters params;
    for (const auto& [name, value] : request.query_params) {
      params.Add({name, value});
    }
    cpr::Response response = cpr::Get(cpr::Url{request.url}, header, params,
                                      cpr::Timeout{kTimeoutMs});
    if (response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT) {
      return absl::DeadlineExceededError(
          absl::StrCat("GET ", request.url, " timed out"));
    }
    if (response.error.code != cpr::ErrorCode::OK) {
      return absl::UnavailableError(absl::StrCat(
          "GET ", request.url, " failed: ", response.error.message));
    }
    return HttpResponse{static_cast<int>(response.status_code),
                        std::move(response.text)};
  }
};

}  // namespace

std::unique_ptr<HttpClient> CreateHttpClient() {
  return std::make_unique<CprHttpClient>();
}

}  // namespace firefly
