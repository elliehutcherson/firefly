#include "common/config.h"

#include <cstdlib>

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace firefly {
namespace {

constexpr int kMaxPort = 65535;

// Overwrites `field` when the variable holds a positive integer; invalid
// values are ignored, like FIREFLY_PORT.
void ReadPositiveInt(const char* name, int* field) {
  const char* raw = std::getenv(name);
  if (raw == nullptr) {
    return;
  }
  int value = 0;
  if (absl::SimpleAtoi(raw, &value) && value > 0) {
    *field = value;
  }
}

}  // namespace

Config Config::FromEnv() {
  Config config;
  if (const char* bind = std::getenv("FIREFLY_BIND")) {
    config.bind_address = bind;
  }
  if (const char* port = std::getenv("FIREFLY_PORT")) {
    int value = 0;
    if (absl::SimpleAtoi(port, &value) && value > 0 && value <= kMaxPort) {
      config.port = value;
    }
  }
  if (const char* url = std::getenv("DATABASE_URL")) {
    config.database_url = url;
  }
  if (const char* key_id = std::getenv("APCA_API_KEY_ID")) {
    config.alpaca_key_id = key_id;
  }
  if (const char* secret_key = std::getenv("APCA_API_SECRET_KEY")) {
    config.alpaca_secret_key = secret_key;
  }
  if (const char* secret = std::getenv("TURNSTILE_SECRET_KEY")) {
    config.turnstile_secret_key = secret;
  }
  if (const char* header = std::getenv("FIREFLY_CLIENT_IP_HEADER")) {
    config.client_ip_header = header;
  }
  ReadPositiveInt("FIREFLY_SESSION_TTL_DAYS", &config.session_ttl_days);
  ReadPositiveInt("FIREFLY_DB_POOL_SIZE", &config.db_pool_size);
  ReadPositiveInt("FIREFLY_DB_ACQUIRE_TIMEOUT_MS",
                  &config.db_acquire_timeout_ms);
  ReadPositiveInt("FIREFLY_SIGNUP_IP_DAILY_CAP", &config.signup_ip_daily_cap);
  ReadPositiveInt("FIREFLY_AUTH_RATE_PER_MIN", &config.auth_rate_per_min);
  ReadPositiveInt("FIREFLY_AUTH_BURST", &config.auth_burst);
  if (const char* allow = std::getenv("FIREFLY_ALLOW_CLOSED_MARKET_TRADING")) {
    const absl::string_view value(allow);
    config.allow_closed_market_trading = value == "1" || value == "true";
  }
  return config;
}

}  // namespace firefly
