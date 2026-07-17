#include "marketdata/cached_instrument_store.h"

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
#include "marketdata/instrument_store.h"
#include "support/status_matchers.h"
#include "support/symbol.h"

namespace firefly {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;

class FakeInstrumentStore : public InstrumentStore {
 public:
  absl::StatusOr<std::vector<Symbol>> ListActiveSymbols() override {
    ++list_calls;
    return list();
  }

  absl::StatusOr<bool> Exists(const Symbol&) override {
    return absl::InternalError("cache must not query source membership");
  }

  int list_calls = 0;
  std::function<absl::StatusOr<std::vector<Symbol>>()> list;
};

TEST(CachedInstrumentStoreTest, RequiresInitialRefresh) {
  FakeInstrumentStore source;
  CachedInstrumentStore cache(source);

  EXPECT_THAT(cache.ListActiveSymbols(),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(cache.Exists(Sym("AAPL")),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST(CachedInstrumentStoreTest, RefreshPublishesSortedUniqueSnapshot) {
  FakeInstrumentStore source;
  source.list = [] {
    return std::vector<Symbol>{Sym("MSFT"), Sym("AAPL"), Sym("MSFT")};
  };
  CachedInstrumentStore cache(source);

  ASSERT_OK(cache.Refresh());

  EXPECT_THAT(cache.ListActiveSymbols(),
              IsOkAndHolds(ElementsAre(Sym("AAPL"), Sym("MSFT"))));
  EXPECT_THAT(cache.Exists(Sym("AAPL")), IsOkAndHolds(true));
  EXPECT_THAT(cache.Exists(Sym("GOOG")), IsOkAndHolds(false));
  EXPECT_EQ(source.list_calls, 1);
}

TEST(CachedInstrumentStoreTest, FailedRefreshKeepsLastGoodSnapshot) {
  FakeInstrumentStore source;
  source.list = [] { return std::vector<Symbol>{Sym("AAPL")}; };
  CachedInstrumentStore cache(source);
  ASSERT_OK(cache.Refresh());

  source.list = [] {
    return absl::UnavailableError("database temporarily unavailable");
  };
  EXPECT_THAT(cache.Refresh(), StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(cache.Exists(Sym("AAPL")), IsOkAndHolds(true));

  source.list = [] { return std::vector<Symbol>{}; };
  EXPECT_THAT(cache.Refresh(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(cache.Exists(Sym("AAPL")), IsOkAndHolds(true));
}

TEST(CachedInstrumentStoreTest, ReadersSeeCompleteSnapshotDuringRefresh) {
  FakeInstrumentStore source;
  source.list = [] { return std::vector<Symbol>{Sym("AAPL")}; };
  CachedInstrumentStore cache(source);
  ASSERT_OK(cache.Refresh());

  absl::Notification entered;
  absl::Notification release;
  source.list = [&] {
    entered.Notify();
    release.WaitForNotification();
    return absl::StatusOr<std::vector<Symbol>>(
        std::vector<Symbol>{Sym("MSFT")});
  };
  absl::Status refresh_status;
  std::thread refresh([&] { refresh_status = cache.Refresh(); });
  entered.WaitForNotification();

  EXPECT_THAT(cache.Exists(Sym("AAPL")), IsOkAndHolds(true));
  EXPECT_THAT(cache.Exists(Sym("MSFT")), IsOkAndHolds(false));
  release.Notify();
  refresh.join();

  EXPECT_TRUE(refresh_status.ok());
  EXPECT_THAT(cache.Exists(Sym("AAPL")), IsOkAndHolds(false));
  EXPECT_THAT(cache.Exists(Sym("MSFT")), IsOkAndHolds(true));
}

}  // namespace
}  // namespace firefly
