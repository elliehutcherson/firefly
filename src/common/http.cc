#include "src/common/http.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpr/cpr.h"  // IWYU pragma: keep
#include "src/common/http_cpr.h"

namespace firefly {
namespace {

constexpr int kTimeoutMs = 10000;

TransportResult DoTransport(const HttpRequest& request) {
  cpr::Header header;
  for (const auto& [name, value] : request.headers) {
    header.emplace(name, value);
  }
  cpr::Parameters params;
  for (const auto& [name, value] : request.query_params) {
    params.Add({name, value});
  }
  cpr::Response response =
      cpr::Get(cpr::Url{request.url}, header, params, cpr::Timeout{kTimeoutMs});
  return TransportResult{
      .ok = response.error.code == cpr::ErrorCode::OK,
      .timed_out = response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT,
      .error_message = response.error.message,
      .status_code = static_cast<int>(response.status_code),
      .body = std::move(response.text),
  };
}

}  // namespace

CprHttpClient::CprHttpClient(Options options)
    : transport_cb_(std::move(options.transport)) {
  if (!transport_cb_) {
    transport_cb_ = &DoTransport;
  }
}

absl::StatusOr<HttpResponse> CprHttpClient::Get(const HttpRequest& request) {
  TransportResult result = transport_cb_(request);
  if (result.timed_out) {
    return absl::DeadlineExceededError(
        absl::StrCat("GET ", request.url, " timed out"));
  }
  if (!result.ok) {
    return absl::UnavailableError(
        absl::StrCat("GET ", request.url, " failed: ", result.error_message));
  }
  return HttpResponse{result.status_code, std::move(result.body)};
}

std::unique_ptr<HttpClient> CreateHttpClient() {
  return std::make_unique<CprHttpClient>();
}

}  // namespace firefly
