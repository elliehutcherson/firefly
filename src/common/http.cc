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

TransportResult DoTransport(const HttpRequest& request, HttpMethod method) {
  cpr::Header header;
  for (const auto& [name, value] : request.headers) {
    header.emplace(name, value);
  }
  cpr::Parameters params;
  for (const auto& [name, value] : request.query_params) {
    params.Add({name, value});
  }
  cpr::Response response;
  if (method == HttpMethod::kPost) {
    // cpr::Payload URL-encodes and sets application/x-www-form-urlencoded.
    cpr::Payload payload({});
    for (const auto& [name, value] : request.form) {
      payload.Add({name, value});
    }
    response = cpr::Post(cpr::Url{request.url}, header, params, payload,
                         cpr::Timeout{kTimeoutMs});
  } else {
    response = cpr::Get(cpr::Url{request.url}, header, params,
                        cpr::Timeout{kTimeoutMs});
  }
  return TransportResult{
      .ok = response.error.code == cpr::ErrorCode::OK,
      .timed_out = response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT,
      .error_message = response.error.message,
      .status_code = static_cast<int>(response.status_code),
      .body = std::move(response.text),
  };
}

const char* MethodName(HttpMethod method) {
  return method == HttpMethod::kPost ? "POST" : "GET";
}

}  // namespace

CprHttpClient::CprHttpClient(Options options)
    : transport_cb_(std::move(options.transport)) {
  if (!transport_cb_) {
    transport_cb_ = &DoTransport;
  }
}

absl::StatusOr<HttpResponse> CprHttpClient::Perform(const HttpRequest& request,
                                                    HttpMethod method) {
  TransportResult result = transport_cb_(request, method);
  if (result.timed_out) {
    return absl::DeadlineExceededError(
        absl::StrCat(MethodName(method), " ", request.url, " timed out"));
  }
  if (!result.ok) {
    return absl::UnavailableError(absl::StrCat(MethodName(method), " ",
                                               request.url,
                                               " failed: ", result.error_message));
  }
  return HttpResponse{result.status_code, std::move(result.body)};
}

absl::StatusOr<HttpResponse> CprHttpClient::Get(const HttpRequest& request) {
  return Perform(request, HttpMethod::kGet);
}

absl::StatusOr<HttpResponse> CprHttpClient::Post(const HttpRequest& request) {
  return Perform(request, HttpMethod::kPost);
}

std::unique_ptr<HttpClient> CreateHttpClient() {
  return std::make_unique<CprHttpClient>();
}

}  // namespace firefly
