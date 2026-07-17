#include "api/server.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "fakes/db/fake_db.h"

namespace firefly {
namespace {

TEST(CheckHealthTest, HealthyDbIsOk) {
  FakeDb db;
  const HealthReport report = CheckHealth(db);
  EXPECT_EQ(report.http_status, 200);
  EXPECT_TRUE(report.db_ok);
}

TEST(CheckHealthTest, DbOutageIsDegraded) {
  FakeDb db;
  db.ping_status = absl::UnavailableError("connection refused");
  const HealthReport report = CheckHealth(db);
  EXPECT_EQ(report.http_status, 503);
  EXPECT_FALSE(report.db_ok);
}

}  // namespace
}  // namespace firefly
