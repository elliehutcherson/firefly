#include "src/common/config.h"

#include <cstdlib>

#include "absl/strings/numbers.h"

namespace firefly {
namespace {

constexpr int kMaxPort = 65535;

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
  return config;
}

}  // namespace firefly
