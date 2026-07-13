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
    unsetenv("APCA_API_KEY_ID");
    unsetenv("APCA_API_SECRET_KEY");
  }
};

TEST_F(ConfigTest, Defaults) {
  Config config = Config::FromEnv();
  EXPECT_EQ(config.bind_address, "127.0.0.1");
  EXPECT_EQ(config.port, 8080);
  EXPECT_EQ(config.database_url,
            "postgres://firefly:firefly@localhost:5432/firefly");
  EXPECT_TRUE(config.alpaca_key_id.empty());
  EXPECT_TRUE(config.alpaca_secret_key.empty());
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
