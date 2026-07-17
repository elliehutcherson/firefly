#include "src/api/trading_handlers.h"

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "src/auth/crypto.h"
#include "src/common/clock.h"
#include "src/marketdata/instrument_repo.h"
#include "src/marketdata/provider.h"
#include "src/trading/order_math.h"
#include "src/trading/trading_store.h"
#include "tests/fakes/auth/fake_session_store.h"
#include "tests/fakes/common/fake_clock.h"
#include "tests/fakes/db/fake_db.h"
#include "tests/fakes/marketdata/fake_market_data_provider.h"
#include "tests/fakes/trading/fake_trading_store.h"
#include "tests/support/status_matchers.h"
#include "tests/support/symbol.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

constexpr char kToken[] = "deadbeef";
constexpr char kBuyTwoAapl[] =
    R"({"symbol":"AAPL","side":"buy","quantity":2})";

// Wednesday 2026-07-15, 14:00 New York: a regular open trading afternoon.
absl::Time OpenNow() {
  return absl::FromCivil(absl::CivilHour(2026, 7, 15, 14), NewYorkTimeZone());
}

class TradingHandlersTest : public ::testing::Test {
 protected:
  TradingDeps Deps() {
    return {.trading = trading_,
            .instruments = instruments_,
            .provider = &provider_,
            .session_auth = {.sessions = sessions_,
                             .clock = clock_,
                             .session_ttl = absl::Hours(24 * 30)},
            .clock = clock_,
            .allow_closed_market = allow_closed_market_};
  }

  void SignIn() {
    sessions_.sessions[Sha256Hex(kToken)] = {
        .user_id = 42,
        .last_seen_at = clock_.Now(),
        .expires_at = clock_.Now() + absl::Hours(24)};
  }

  void SymbolExists() { db_.query_results.push_back(Rows{Row{{"1"}}}); }
  void SymbolMissing() { db_.query_results.push_back(Rows{}); }

  void CanQuote(int64_t price_e4) {
    provider_.latest_trade_results.push_back(
        Trade{.price_e4 = price_e4, .time = clock_.Now()});
  }

  FakeDb db_;
  InstrumentRepo instruments_{db_};
  FakeTradingStore trading_;
  FakeMarketDataProvider provider_;
  FakeSessionStore sessions_;
  FakeClock clock_{OpenNow()};
  bool allow_closed_market_ = false;
};

TEST_F(TradingHandlersTest, OrderRequiresASession) {
  EXPECT_THAT(PlaceOrderJson(Deps(), "", kBuyTwoAapl),
              StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_THAT(PlaceOrderJson(Deps(), kToken, kBuyTwoAapl),
              StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_TRUE(db_.calls.empty());
  EXPECT_TRUE(trading_.execute_calls.empty());
}

TEST_F(TradingHandlersTest, MalformedBodiesAre400) {
  SignIn();
  const char* bad_bodies[] = {
      "not json",
      "[]",
      R"({"side":"buy","quantity":2})",                     // No symbol.
      R"({"symbol":"bad sym","side":"buy","quantity":2})",  // Junk symbol.
      R"({"symbol":"AAPL","quantity":2})",                  // No side.
      R"({"symbol":"AAPL","side":"hold","quantity":2})",    // Junk side.
      R"({"symbol":"AAPL","side":"buy"})",                  // No quantity.
      R"({"symbol":"AAPL","side":"buy","quantity":1.5})",   // Not integral.
      R"({"symbol":"AAPL","side":"buy","quantity":"2"})",   // String.
      R"({"symbol":"AAPL","side":"buy","quantity":0})",
      R"({"symbol":"AAPL","side":"buy","quantity":-1})",
      R"({"symbol":"AAPL","side":"buy","quantity":1000001})",
  };
  for (const char* body : bad_bodies) {
    EXPECT_THAT(PlaceOrderJson(Deps(), kToken, body),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "body: " << body;
  }
  EXPECT_TRUE(trading_.execute_calls.empty());
}

TEST_F(TradingHandlersTest, UnknownSymbolIs404BeforeAnyQuote) {
  SignIn();
  SymbolMissing();
  EXPECT_THAT(PlaceOrderJson(Deps(), kToken, kBuyTwoAapl),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_TRUE(provider_.latest_trade_calls.empty());
  EXPECT_TRUE(trading_.execute_calls.empty());
}

TEST_F(TradingHandlersTest, ClosedMarketIsRejectedBeforeAnyQuote) {
  SignIn();
  for (const absl::CivilSecond closed : {
           absl::CivilSecond(2026, 7, 18, 12, 0, 0),  // Saturday
           absl::CivilSecond(2026, 7, 15, 16, 0, 0),  // At the close
           absl::CivilSecond(2026, 4, 3, 12, 0, 0),   // Good Friday
       }) {
    clock_.SetNow(absl::FromCivil(closed, NewYorkTimeZone()));
    SignIn();  // Keep the session alive at the new time.
    SymbolExists();
    EXPECT_THAT(PlaceOrderJson(Deps(), kToken, kBuyTwoAapl),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("market is closed")))
        << absl::FormatCivilTime(closed);
  }
  EXPECT_TRUE(provider_.latest_trade_calls.empty());
  EXPECT_TRUE(trading_.execute_calls.empty());
}

TEST_F(TradingHandlersTest, BypassFlagAllowsClosedMarketTrading) {
  allow_closed_market_ = true;
  clock_.SetNow(absl::FromCivil(absl::CivilSecond(2026, 7, 18, 12, 0, 0),
                                NewYorkTimeZone()));  // Saturday
  SignIn();
  SymbolExists();
  CanQuote(1000000);
  trading_.order_results.push_back(OrderResult{.order_id = 7,
                                               .price_e4 = 1000000,
                                               .cash_delta_cents = -20000,
                                               .cash_cents = 980000,
                                               .position_quantity = 2,
                                               .position_avg_price_e4 = 1000000});
  EXPECT_OK(PlaceOrderJson(Deps(), kToken, kBuyTwoAapl));
}

TEST_F(TradingHandlersTest, MissingProviderIs503) {
  SignIn();
  SymbolExists();
  TradingDeps deps = Deps();
  deps.provider = nullptr;
  EXPECT_THAT(PlaceOrderJson(deps, kToken, kBuyTwoAapl),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_TRUE(trading_.execute_calls.empty());
}

TEST_F(TradingHandlersTest, ForwardsTheQuotePriceAndFormatsTheResult) {
  SignIn();
  SymbolExists();
  CanQuote(1899550);
  trading_.order_results.push_back(OrderResult{.order_id = 77,
                                               .price_e4 = 1899550,
                                               .cash_delta_cents = -37991,
                                               .cash_cents = 962009,
                                               .position_quantity = 2,
                                               .position_avg_price_e4 = 1899550});

  absl::StatusOr<nlohmann::json> body =
      PlaceOrderJson(Deps(), kToken, kBuyTwoAapl);
  ASSERT_OK(body);
  EXPECT_EQ((*body)["order_id"], 77);
  EXPECT_EQ((*body)["symbol"], "AAPL");
  EXPECT_EQ((*body)["side"], "buy");
  EXPECT_EQ((*body)["quantity"], 2);
  EXPECT_EQ((*body)["price"], "189.9550");
  EXPECT_EQ((*body)["cash_delta_cents"], -37991);
  EXPECT_EQ((*body)["cash_cents"], 962009);
  EXPECT_EQ((*body)["position"]["quantity"], 2);
  EXPECT_EQ((*body)["position"]["avg_price"], "189.9550");

  ASSERT_EQ(trading_.execute_calls.size(), 1);
  const FakeExecuteOrderCall& call = trading_.execute_calls[0];
  EXPECT_EQ(call.user_id, 42);
  EXPECT_EQ(call.symbol, "AAPL");
  EXPECT_EQ(call.side, OrderSide::kBuy);
  EXPECT_EQ(call.quantity, 2);
  EXPECT_EQ(call.price_e4, 1899550);  // The quote price, verbatim.
}

TEST_F(TradingHandlersTest, PositionIsNullWhenTheOrderLeavesTheAccountFlat) {
  SignIn();
  SymbolExists();
  CanQuote(1000000);
  trading_.order_results.push_back(OrderResult{.order_id = 8,
                                               .price_e4 = 1000000,
                                               .cash_delta_cents = 20000,
                                               .cash_cents = 1000000,
                                               .position_quantity = 0,
                                               .position_avg_price_e4 = 0});

  absl::StatusOr<nlohmann::json> body = PlaceOrderJson(
      Deps(), kToken, R"({"symbol":"AAPL","side":"sell","quantity":2})");
  ASSERT_OK(body);
  EXPECT_TRUE((*body)["position"].is_null());
}

TEST_F(TradingHandlersTest, StateRejectionsPropagateAsFailedPrecondition) {
  SignIn();
  SymbolExists();
  CanQuote(1000000);
  trading_.order_results.push_back(
      absl::FailedPreconditionError("insufficient cash"));
  EXPECT_THAT(PlaceOrderJson(Deps(), kToken, kBuyTwoAapl),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("insufficient cash")));
}

TEST_F(TradingHandlersTest, PortfolioRequiresASession) {
  EXPECT_THAT(GetPortfolioJson(Deps(), ""),
              StatusIs(absl::StatusCode::kUnauthenticated));
  EXPECT_TRUE(trading_.portfolio_calls.empty());
}

TEST_F(TradingHandlersTest, PortfolioSerializesPositions) {
  SignIn();
  Portfolio portfolio;
  portfolio.cash_cents = 962009;
  portfolio.positions.push_back(
      PositionRecord{.symbol = Sym("AAPL"), .quantity = 2,
                     .avg_price_e4 = 1899550});
  portfolio.positions.push_back(
      PositionRecord{.symbol = Sym("TSLA"), .quantity = -1,
                     .avg_price_e4 = 2500000});
  trading_.portfolio_results.push_back(portfolio);

  absl::StatusOr<nlohmann::json> body = GetPortfolioJson(Deps(), kToken);
  ASSERT_OK(body);
  EXPECT_EQ((*body)["cash_cents"], 962009);
  ASSERT_EQ((*body)["positions"].size(), 2);
  EXPECT_EQ((*body)["positions"][0]["symbol"], "AAPL");
  EXPECT_EQ((*body)["positions"][0]["avg_price"], "189.9550");
  EXPECT_EQ((*body)["positions"][1]["quantity"], -1);
  ASSERT_EQ(trading_.portfolio_calls.size(), 1);
  EXPECT_EQ(trading_.portfolio_calls[0], 42);
}

TEST_F(TradingHandlersTest, PortfolioFlatAccountHasEmptyArray) {
  SignIn();
  trading_.portfolio_results.push_back(Portfolio{.cash_cents = 1000000});

  absl::StatusOr<nlohmann::json> body = GetPortfolioJson(Deps(), kToken);
  ASSERT_OK(body);
  EXPECT_TRUE((*body)["positions"].is_array());
  EXPECT_TRUE((*body)["positions"].empty());
}

}  // namespace
}  // namespace firefly
