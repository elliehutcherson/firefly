#include "src/marketdata/cached_instrument_store.h"

#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/marketdata/instrument_store.h"
#include "tests/support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;

class FakeInstrumentStore : public InstrumentStore {
 public:
  absl::StatusOr<std::vector<std::string>> ListActiveSymbols() override {
    ++list_calls;
    return list();
  }

  absl::StatusOr<bool> Exists(const std::string&) override {
    return absl::InternalError("cache must not query source membership");
  }

  int list_calls = 0;
  std::function<absl::StatusOr<std::vector<std::string>>()> list;
};

TEST(CachedInstrumentStoreTest, RequiresInitialRefresh) {
  FakeInstrumentStore source;
  CachedInstrumentStore cache(source);

  EXPECT_THAT(cache.ListActiveSymbols(),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(cache.Exists("AAPL"),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(CachedInstrumentStoreTest, RefreshPublishesSortedUniqueSnapshot) {
  FakeInstrumentStore source;
  source.list = [] {
    return std::vector<std::string>{"MSFT", "AAPL", "MSFT"};
  };
  CachedInstrumentStore cache(source);

  ASSERT_OK(cache.Refresh());

  EXPECT_THAT(cache.ListActiveSymbols(),
              IsOkAndHolds(ElementsAre("AAPL", "MSFT")));
  EXPECT_THAT(cache.Exists("AAPL"), IsOkAndHolds(true));
  EXPECT_THAT(cache.Exists("GOOG"), IsOkAndHolds(false));
  EXPECT_EQ(source.list_calls, 1);
}

TEST(CachedInstrumentStoreTest, FailedRefreshKeepsLastGoodSnapshot) {
  FakeInstrumentStore source;
  source.list = [] { return std::vector<std::string>{"AAPL"}; };
  CachedInstrumentStore cache(source);
  ASSERT_OK(cache.Refresh());

  source.list = [] {
    return absl::UnavailableError("database temporarily unavailable");
  };
  EXPECT_THAT(cache.Refresh(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(cache.Exists("AAPL"), IsOkAndHolds(true));

  source.list = [] { return std::vector<std::string>{}; };
  EXPECT_THAT(cache.Refresh(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(cache.Exists("AAPL"), IsOkAndHolds(true));
}

TEST(CachedInstrumentStoreTest, ReadersSeeCompleteSnapshotDuringRefresh) {
  FakeInstrumentStore source;
  source.list = [] { return std::vector<std::string>{"AAPL"}; };
  CachedInstrumentStore cache(source);
  ASSERT_OK(cache.Refresh());

  absl::Notification entered;
  absl::Notification release;
  source.list = [&] {
    entered.Notify();
    release.WaitForNotification();
    return absl::StatusOr<std::vector<std::string>>(
        std::vector<std::string>{"MSFT"});
  };
  absl::Status refresh_status;
  std::thread refresh([&] { refresh_status = cache.Refresh(); });
  entered.WaitForNotification();

  EXPECT_THAT(cache.Exists("AAPL"), IsOkAndHolds(true));
  EXPECT_THAT(cache.Exists("MSFT"), IsOkAndHolds(false));
  release.Notify();
  refresh.join();

  EXPECT_TRUE(refresh_status.ok());
  EXPECT_THAT(cache.Exists("AAPL"), IsOkAndHolds(false));
  EXPECT_THAT(cache.Exists("MSFT"), IsOkAndHolds(true));
}

}  // namespace
}  // namespace firefly
