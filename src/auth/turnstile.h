#ifndef FIREFLY_AUTH_TURNSTILE_H_
#define FIREFLY_AUTH_TURNSTILE_H_

#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "src/common/http.h"

namespace firefly {

// Server-side Cloudflare Turnstile verification (docs/ARCHITECTURE.md,
// "Anti-abuse"). The browser widget produces a one-time response token; we
// confirm it with Cloudflare before creating accounts or sessions.
//
// An empty secret disables verification entirely (dev/test — the Alpaca
// empty-creds pattern); main.cc logs that state once at startup.
class TurnstileVerifier {
 public:
  // `http` is borrowed and must outlive the verifier; unused when the
  // secret is empty.
  TurnstileVerifier(std::string secret_key, HttpClient& http)
      : secret_key_(std::move(secret_key)), http_(http) {}

  bool enabled() const { return !secret_key_.empty(); }

  // OK when the token is valid (or verification is disabled).
  // PermissionDenied when Cloudflare rejects it; Unavailable /
  // DeadlineExceeded when Cloudflare cannot be reached.
  absl::Status Verify(const std::string& response_token,
                      const std::optional<std::string>& remote_ip) const;

 private:
  const std::string secret_key_;
  HttpClient& http_;
};

}  // namespace firefly

#endif  // FIREFLY_AUTH_TURNSTILE_H_
