#include "src/marketdata/alpaca.h"

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/common/http.h"
#include "src/marketdata/provider.h"
#include "tests/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::Contains;
using ::testing::Pair;

// Replays canned responses and records every request it receives.
class FakeHttpClient : public HttpClient {
 public:
  absl::StatusOr<HttpResponse> Get(const HttpRequest& request) override {
    requests.push_back(request);
    if (responses.empty()) {
      return absl::InternalError("FakeHttpClient: no canned response left");
    }
    absl::StatusOr<HttpResponse> response = std::move(responses.front());
    responses.pop_front();
    return response;
  }

  std::vector<HttpRequest> requests;
  std::deque<absl::StatusOr<HttpResponse>> responses;
};

absl::Time UtcTime(int year, int month, int day, int hour, int minute,
                   int second) {
  return absl::FromCivil(
      absl::CivilSecond(year, month, day, hour, minute, second),
      absl::UTCTimeZone());
}

class AlpacaTest : public ::testing::Test {
 protected:
  AlpacaProvider MakeProvider() {
    return AlpacaProvider({.key_id = "test-key", .secret_key = "test-secret"},
                          &http_);
  }

  FakeHttpClient http_;
};

TEST_F(AlpacaTest, LatestTradeParsesPriceAndTime) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"symbol":"AAPL","trade":{"t":"2026-07-10T19:59:58.123Z","p":189.955,"s":100}})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<Trade> trade = provider.GetLatestTrade("AAPL");
  ASSERT_OK(trade);
  EXPECT_EQ(trade->price_e4, 1899550);
  EXPECT_EQ(trade->time,
            UtcTime(2026, 7, 10, 19, 59, 58) + absl::Milliseconds(123));

  ASSERT_EQ(http_.requests.size(), 1);
  const HttpRequest& request = http_.requests[0];
  EXPECT_EQ(request.url,
            "https://data.alpaca.markets/v2/stocks/AAPL/trades/latest");
  EXPECT_THAT(request.headers, Contains(Pair("APCA-API-KEY-ID", "test-key")));
  EXPECT_THAT(request.headers,
              Contains(Pair("APCA-API-SECRET-KEY", "test-secret")));
  EXPECT_THAT(request.query_params, Contains(Pair("feed", "iex")));
}

TEST_F(AlpacaTest, DailyBarsParseAndRequestTheSipFeed) {
  http_.responses.push_back(HttpResponse{200,
                                         R"({"bars":[
            {"t":"2026-01-02T05:00:00Z","o":100.5,"h":101.0,"l":99.25,"c":100.7501,"v":1200000},
            {"t":"2026-01-05T05:00:00Z","o":100.8,"h":102.0,"l":100.1,"c":101.9,"v":980000}],
          "symbol":"AAPL","next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetDailyBars(
      "AAPL", absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
  ASSERT_OK(bars);
  ASSERT_EQ(bars->size(), 2);
  EXPECT_EQ((*bars)[0].time, UtcTime(2026, 1, 2, 5, 0, 0));
  EXPECT_EQ((*bars)[0].open_e4, 1005000);
  EXPECT_EQ((*bars)[0].high_e4, 1010000);
  EXPECT_EQ((*bars)[0].low_e4, 992500);
  EXPECT_EQ((*bars)[0].close_e4, 1007501);
  EXPECT_EQ((*bars)[0].volume, 1200000);
  EXPECT_EQ((*bars)[1].close_e4, 1019000);

  ASSERT_EQ(http_.requests.size(), 1);
  const HttpRequest& request = http_.requests[0];
  EXPECT_EQ(request.url, "https://data.alpaca.markets/v2/stocks/AAPL/bars");
  EXPECT_THAT(request.query_params, Contains(Pair("timeframe", "1Day")));
  EXPECT_THAT(request.query_params, Contains(Pair("start", "2026-01-02")));
  EXPECT_THAT(request.query_params, Contains(Pair("end", "2026-01-31")));
  EXPECT_THAT(request.query_params, Contains(Pair("feed", "sip")));
  EXPECT_THAT(request.query_params, Contains(Pair("adjustment", "split")));
}

TEST_F(AlpacaTest, DailyBarsFollowPagination) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-02T05:00:00Z","o":1,"h":1,"l":1,"c":1,"v":1}],
          "next_page_token":"abc123"})"});
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-05T05:00:00Z","o":2,"h":2,"l":2,"c":2,"v":2}],
          "next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetDailyBars(
      "AAPL", absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
  ASSERT_OK(bars);
  EXPECT_EQ(bars->size(), 2);

  ASSERT_EQ(http_.requests.size(), 2);
  EXPECT_THAT(http_.requests[1].query_params,
              Contains(Pair("page_token", "abc123")));
}

TEST_F(AlpacaTest, MinuteBarsRequestRfc3339Range) {
  http_.responses.push_back(
      HttpResponse{200, R"({"bars":null,"next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetMinuteBars(
      "AAPL", UtcTime(2026, 7, 10, 13, 30, 0), UtcTime(2026, 7, 10, 19, 45, 0));
  ASSERT_OK(bars);
  EXPECT_TRUE(bars->empty());

  ASSERT_EQ(http_.requests.size(), 1);
  const HttpRequest& request = http_.requests[0];
  EXPECT_THAT(request.query_params, Contains(Pair("timeframe", "1Min")));
  EXPECT_THAT(request.query_params,
              Contains(Pair("start", "2026-07-10T13:30:00Z")));
  EXPECT_THAT(request.query_params,
              Contains(Pair("end", "2026-07-10T19:45:00Z")));
}

TEST_F(AlpacaTest, RateLimitIsResourceExhausted) {
  http_.responses.push_back(HttpResponse{429, "too many requests"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(AlpacaTest, BadCredentialsArePermissionDenied) {
  http_.responses.push_back(HttpResponse{403, "forbidden"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(AlpacaTest, MalformedJsonIsInternal) {
  http_.responses.push_back(HttpResponse{200, "not json at all"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AlpacaTest, MissingFieldIsInternal) {
  http_.responses.push_back(
      HttpResponse{200, R"({"symbol":"AAPL","trade":{"t":"bad","s":1}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AlpacaTest, TransportErrorPropagates) {
  http_.responses.push_back(absl::UnavailableError("connection refused"));
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("AAPL"),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(AlpacaTest, BadSymbolIsRejectedBeforeAnyRequest) {
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade("aapl"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(provider.GetLatestTrade("AAPL/../.."),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(provider.GetDailyBars("", absl::CivilDay(2026, 1, 1),
                                    absl::CivilDay(2026, 1, 2)),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(http_.requests.empty());
}

}  // namespace
}  // namespace firefly
