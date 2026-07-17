#include "auth/turnstile.h"

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"
#include "common/http.h"
#include "common/status_macros.h"

namespace firefly {
namespace {

constexpr char kSiteVerifyUrl[] =
    "https://challenges.cloudflare.com/turnstile/v0/siteverify";

}  // namespace

absl::Status TurnstileVerifier::Verify(
    const std::string& response_token,
    const std::optional<std::string>& remote_ip) const {
  if (!enabled()) {
    return absl::OkStatus();
  }

  HttpRequest request{.url = kSiteVerifyUrl,
                      .form = {{"secret", secret_key_},
                               {"response", response_token}}};
  if (remote_ip.has_value()) {
    request.form.emplace_back("remoteip", *remote_ip);
  }
  ASSIGN_OR_RETURN(const HttpResponse response, http_.Post(request));
  if (response.status_code != 200) {
    return absl::UnavailableError(
        absl::StrCat("turnstile siteverify returned ", response.status_code));
  }
  const nlohmann::json body = nlohmann::json::parse(
      response.body, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (body.is_discarded() || !body.contains("success")) {
    return absl::UnavailableError("turnstile siteverify returned junk");
  }
  if (body["success"] != true) {
    return absl::PermissionDeniedError("turnstile verification failed");
  }
  return absl::OkStatus();
}

}  // namespace firefly
