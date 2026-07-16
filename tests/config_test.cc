#include "src/common/config.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace firefly {
namespace {

class ConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    unsetenv("FIREFLY_BIND");
    unsetenv("FIREFLY_PORT");
    unsetenv("DATABASE_URL");
    unsetenv("FIREFLY_DB_POOL_SIZE");
    unsetenv("FIREFLY_DB_ACQUIRE_TIMEOUT_MS");
    unsetenv("APCA_API_KEY_ID");
    unsetenv("APCA_API_SECRET_KEY");
    unsetenv("TURNSTILE_SECRET_KEY");
    unsetenv("FIREFLY_CLIENT_IP_HEADER");
    unsetenv("FIREFLY_SESSION_TTL_DAYS");
    unsetenv("FIREFLY_SIGNUP_IP_DAILY_CAP");
    unsetenv("FIREFLY_AUTH_RATE_PER_MIN");
    unsetenv("FIREFLY_AUTH_BURST");
  }
};

TEST_F(ConfigTest, Defaults) {
  Config config = Config::FromEnv();
  EXPECT_EQ(config.bind_address, "127.0.0.1");
  EXPECT_EQ(config.port, 8080);
  EXPECT_EQ(config.database_url,
            "postgres://firefly:firefly@localhost:5432/firefly");
  EXPECT_EQ(config.db_pool_size, 4);
  EXPECT_EQ(config.db_acquire_timeout_ms, 2000);
  EXPECT_TRUE(config.alpaca_key_id.empty());
  EXPECT_TRUE(config.alpaca_secret_key.empty());
  EXPECT_TRUE(config.turnstile_secret_key.empty());
  EXPECT_TRUE(config.client_ip_header.empty());
  EXPECT_EQ(config.session_ttl_days, 30);
  EXPECT_EQ(config.signup_ip_daily_cap, 3);
  EXPECT_EQ(config.auth_rate_per_min, 10);
  EXPECT_EQ(config.auth_burst, 10);
}

TEST_F(ConfigTest, ReadsDatabasePoolSettings) {
  setenv("FIREFLY_DB_POOL_SIZE", "8", 1);
  setenv("FIREFLY_DB_ACQUIRE_TIMEOUT_MS", "750", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.db_pool_size, 8);
  EXPECT_EQ(config.db_acquire_timeout_ms, 750);
}

TEST_F(ConfigTest, IgnoresInvalidDatabasePoolSettings) {
  setenv("FIREFLY_DB_POOL_SIZE", "0", 1);
  setenv("FIREFLY_DB_ACQUIRE_TIMEOUT_MS", "eventually", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.db_pool_size, 4);
  EXPECT_EQ(config.db_acquire_timeout_ms, 2000);
}

TEST_F(ConfigTest, ReadsAuthSettings) {
  setenv("TURNSTILE_SECRET_KEY", "ts-secret", 1);
  setenv("FIREFLY_CLIENT_IP_HEADER", "CF-Connecting-IP", 1);
  setenv("FIREFLY_SESSION_TTL_DAYS", "7", 1);
  setenv("FIREFLY_SIGNUP_IP_DAILY_CAP", "5", 1);
  setenv("FIREFLY_AUTH_RATE_PER_MIN", "20", 1);
  setenv("FIREFLY_AUTH_BURST", "40", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.turnstile_secret_key, "ts-secret");
  EXPECT_EQ(config.client_ip_header, "CF-Connecting-IP");
  EXPECT_EQ(config.session_ttl_days, 7);
  EXPECT_EQ(config.signup_ip_daily_cap, 5);
  EXPECT_EQ(config.auth_rate_per_min, 20);
  EXPECT_EQ(config.auth_burst, 40);
}

TEST_F(ConfigTest, IgnoresInvalidAuthNumbers) {
  setenv("FIREFLY_SESSION_TTL_DAYS", "banana", 1);
  setenv("FIREFLY_AUTH_BURST", "-3", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.session_ttl_days, 30);
  EXPECT_EQ(config.auth_burst, 10);
}

TEST_F(ConfigTest, ReadsAlpacaCredentials) {
  setenv("APCA_API_KEY_ID", "key-id", 1);
  setenv("APCA_API_SECRET_KEY", "shh", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.alpaca_key_id, "key-id");
  EXPECT_EQ(config.alpaca_secret_key, "shh");
}

TEST_F(ConfigTest, ReadsEnvironment) {
  setenv("FIREFLY_BIND", "0.0.0.0", 1);
  setenv("FIREFLY_PORT", "9000", 1);
  setenv("DATABASE_URL", "postgres://u:p@db:5432/x", 1);
  Config config = Config::FromEnv();
  EXPECT_EQ(config.bind_address, "0.0.0.0");
  EXPECT_EQ(config.port, 9000);
  EXPECT_EQ(config.database_url, "postgres://u:p@db:5432/x");
}

TEST_F(ConfigTest, IgnoresInvalidPort) {
  setenv("FIREFLY_PORT", "not-a-port", 1);
  EXPECT_EQ(Config::FromEnv().port, 8080);

  setenv("FIREFLY_PORT", "70000", 1);
  EXPECT_EQ(Config::FromEnv().port, 8080);
}

}  // namespace
}  // namespace firefly
