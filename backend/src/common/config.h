#ifndef FIREFLY_COMMON_CONFIG_H_
#define FIREFLY_COMMON_CONFIG_H_

#include <string>

namespace firefly {

struct Config {
  static constexpr int kDefaultPort = 8080;

  // Reads FIREFLY_BIND, FIREFLY_PORT, DATABASE_URL, database pool settings,
  // and the Alpaca
  // credentials from the environment, falling back to the defaults below.
  // Invalid FIREFLY_PORT values are ignored.
  static Config FromEnv();

  // Crow listens on localhost only; Caddy/Cloudflare terminate TLS in front.
  std::string bind_address = "127.0.0.1";
  int port = kDefaultPort;
  std::string database_url =
      "postgres://firefly:firefly@localhost:5432/firefly";
  // Keep this intentionally aligned with the small synchronous HTTP worker
  // deployment. Pool waits are bounded so saturation cannot consume every
  // worker indefinitely.
  int db_pool_size = 4;             // FIREFLY_DB_POOL_SIZE
  int db_acquire_timeout_ms = 2000;  // FIREFLY_DB_ACQUIRE_TIMEOUT_MS
  // APCA_API_KEY_ID / APCA_API_SECRET_KEY — Alpaca's standard variable names,
  // so the same environment works with Alpaca's own tooling. Empty means
  // market data is unavailable.
  std::string alpaca_key_id;
  std::string alpaca_secret_key;

  // TURNSTILE_SECRET_KEY — Cloudflare Turnstile server secret. Empty means
  // signup/login skip human verification (dev/test).
  std::string turnstile_secret_key;
  // FIREFLY_CLIENT_IP_HEADER — trusted header carrying the real client IP
  // ("CF-Connecting-IP" behind Cloudflare). Empty means use the socket peer
  // address (dev). Only safe because Crow binds localhost behind Caddy.
  std::string client_ip_header;
  // Auth knobs; env vars of the same (uppercased) names, invalid values
  // ignored like FIREFLY_PORT.
  int session_ttl_days = 30;       // FIREFLY_SESSION_TTL_DAYS
  int signup_ip_daily_cap = 3;     // FIREFLY_SIGNUP_IP_DAILY_CAP
  int auth_rate_per_min = 10;      // FIREFLY_AUTH_RATE_PER_MIN
  int auth_burst = 10;             // FIREFLY_AUTH_BURST
  // FIREFLY_ALLOW_CLOSED_MARKET_TRADING ("1"/"true") — dev-only bypass of
  // the market-hours check; without it local dev outside 9:30-16:00 ET
  // cannot trade at all. Orders execute at the last cached quote. Never
  // enable in production (and it is absent from docker-compose.yml).
  bool allow_closed_market_trading = false;
};

}  // namespace firefly

#endif  // FIREFLY_COMMON_CONFIG_H_
