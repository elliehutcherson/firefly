#ifndef FIREFLY_COMMON_CONFIG_H_
#define FIREFLY_COMMON_CONFIG_H_

#include <string>

namespace firefly {

struct Config {
  static constexpr int kDefaultPort = 8080;

  // Reads FIREFLY_BIND, FIREFLY_PORT, DATABASE_URL, and the Alpaca
  // credentials from the environment, falling back to the defaults below.
  // Invalid FIREFLY_PORT values are ignored.
  static Config FromEnv();

  // Crow listens on localhost only; Caddy/Cloudflare terminate TLS in front.
  std::string bind_address = "127.0.0.1";
  int port = kDefaultPort;
  std::string database_url =
      "postgres://firefly:firefly@localhost:5432/firefly";
  // APCA_API_KEY_ID / APCA_API_SECRET_KEY — Alpaca's standard variable names,
  // so the same environment works with Alpaca's own tooling. Empty means
  // market data is unavailable.
  std::string alpaca_key_id;
  std::string alpaca_secret_key;
};

}  // namespace firefly

#endif  // FIREFLY_COMMON_CONFIG_H_
