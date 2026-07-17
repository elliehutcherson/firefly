#include "marketdata/alpaca.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "common/http.h"
#include "marketdata/provider.h"
#include "fakes/common/fake_http_client.h"
#include "support/status_matchers.h"
#include "support/symbol.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;

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
                          http_);
  }

  FakeHttpClient http_;
};

TEST_F(AlpacaTest, LatestTradeParsesPriceAndTime) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"symbol":"AAPL","trade":{"t":"2026-07-10T19:59:58.123Z","p":189.955,"s":100}})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<Trade> trade = provider.GetLatestTrade(Sym("AAPL"));
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
      Sym("AAPL"), absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
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
      Sym("AAPL"), absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
  ASSERT_OK(bars);
  EXPECT_EQ(bars->size(), 2);

  ASSERT_EQ(http_.requests.size(), 2);
  EXPECT_THAT(http_.requests[1].query_params,
              Contains(Pair("page_token", "abc123")));
}

TEST_F(AlpacaTest, PaginationGuardTripsAtMaxPages) {
  for (int i = 0; i < AlpacaProvider::kMaxBarPages; ++i) {
    http_.responses.push_back(HttpResponse{
        200,
        absl::StrCat(
            R"({"bars":[{"t":"2026-01-02T05:00:00Z","o":1,"h":1,"l":1,"c":1,"v":1}],)",
            R"("next_page_token":"tok)", i, R"("})")});
  }
  AlpacaProvider provider = MakeProvider();

  EXPECT_THAT(provider.GetDailyBars(Sym("AAPL"), absl::CivilDay(2026, 1, 2),
                                    absl::CivilDay(2026, 1, 31)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("pagination did not terminate")));

  ASSERT_EQ(http_.requests.size(), AlpacaProvider::kMaxBarPages);
  EXPECT_THAT(http_.requests[0].query_params, Not(Contains(Key("page_token"))));
  for (size_t i = 1; i < http_.requests.size(); ++i) {
    EXPECT_THAT(http_.requests[i].query_params,
                Contains(Pair("page_token", absl::StrCat("tok", i - 1))));
  }
}

TEST_F(AlpacaTest, EmptyNextPageTokenTerminates) {
  // Regression: an empty-string token must end pagination rather than
  // silently re-fetching the same page until the guard trips.
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-02T05:00:00Z","o":1,"h":1,"l":1,"c":1,"v":1}],
          "next_page_token":""})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetDailyBars(
      Sym("AAPL"), absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
  ASSERT_OK(bars);
  EXPECT_EQ(bars->size(), 1);
  EXPECT_EQ(http_.requests.size(), 1);
}

TEST_F(AlpacaTest, MissingBarsAndTokenYieldsEmpty) {
  http_.responses.push_back(HttpResponse{200, "{}"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetDailyBars(
      Sym("AAPL"), absl::CivilDay(2026, 1, 2), absl::CivilDay(2026, 1, 31));
  ASSERT_OK(bars);
  EXPECT_TRUE(bars->empty());
  EXPECT_EQ(http_.requests.size(), 1);
}

TEST_F(AlpacaTest, BadBarMidPaginationFailsFast) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-02T05:00:00Z","o":1,"h":1,"l":1,"c":1,"v":1}],
          "next_page_token":"abc123"})"});
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-05T05:00:00Z","o":"x","h":2,"l":2,"c":2,"v":2}],
          "next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();

  EXPECT_THAT(provider.GetDailyBars(Sym("AAPL"), absl::CivilDay(2026, 1, 2),
                                    absl::CivilDay(2026, 1, 31)),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("not a number")));
  EXPECT_EQ(http_.requests.size(), 2);
}

TEST_F(AlpacaTest, BaseUrlOverrideIsUsed) {
  http_.responses.push_back(HttpResponse{200, "{}"});
  AlpacaProvider provider({.key_id = "test-key",
                           .secret_key = "test-secret",
                           .base_url = "http://localhost:9999"},
                          http_);

  ASSERT_OK(provider.GetDailyBars(Sym("AAPL"), absl::CivilDay(2026, 1, 2),
                                  absl::CivilDay(2026, 1, 31)));
  ASSERT_EQ(http_.requests.size(), 1);
  EXPECT_EQ(http_.requests[0].url,
            "http://localhost:9999/v2/stocks/AAPL/bars");
}

TEST_F(AlpacaTest, MinuteBarsRequestRfc3339Range) {
  http_.responses.push_back(
      HttpResponse{200, R"({"bars":null,"next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();

  absl::StatusOr<std::vector<Bar>> bars = provider.GetMinuteBars(
      Sym("AAPL"), UtcTime(2026, 7, 10, 13, 30, 0), UtcTime(2026, 7, 10, 19, 45, 0));
  ASSERT_OK(bars);
  EXPECT_TRUE(bars->empty());

  ASSERT_EQ(http_.requests.size(), 1);
  const HttpRequest& request = http_.requests[0];
  EXPECT_THAT(request.query_params, Contains(Pair("timeframe", "1Min")));
  EXPECT_THAT(request.query_params,
              Contains(Pair("start", "2026-07-10T13:30:00Z")));
  EXPECT_THAT(request.query_params,
              Contains(Pair("end", "2026-07-10T19:45:00Z")));
  EXPECT_THAT(request.query_params, Contains(Pair("adjustment", "raw")));
  EXPECT_THAT(request.query_params, Contains(Pair("feed", "sip")));
  EXPECT_THAT(request.query_params, Contains(Pair("limit", "10000")));
}

TEST_F(AlpacaTest, RateLimitIsResourceExhausted) {
  http_.responses.push_back(HttpResponse{429, "too many requests"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(AlpacaTest, BadCredentialsArePermissionDenied) {
  http_.responses.push_back(HttpResponse{403, "forbidden"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(AlpacaTest, Http401IsPermissionDenied) {
  http_.responses.push_back(HttpResponse{401, "unauthorized"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(AlpacaTest, Http404IsNotFound) {
  http_.responses.push_back(HttpResponse{404, "no such symbol"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(AlpacaTest, Http422IsInvalidArgument) {
  http_.responses.push_back(HttpResponse{422, "range too recent"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetDailyBars(Sym("AAPL"), absl::CivilDay(2026, 1, 2),
                                    absl::CivilDay(2026, 1, 31)),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AlpacaTest, Http5xxIsUnavailable) {
  http_.responses.push_back(HttpResponse{500, "internal server error"});
  http_.responses.push_back(HttpResponse{503, "service unavailable"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(AlpacaTest, UnexpectedHttpCodeIsInternal) {
  http_.responses.push_back(HttpResponse{418, "i'm a teapot"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("HTTP 418")));
}

TEST_F(AlpacaTest, ErrorBodyIsTruncated) {
  const std::string head(200, 'a');
  http_.responses.push_back(HttpResponse{500, head + "ZZZTAIL"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kUnavailable,
                       AllOf(HasSubstr(head), Not(HasSubstr("ZZZTAIL")))));
}

TEST_F(AlpacaTest, MalformedJsonIsInternal) {
  http_.responses.push_back(HttpResponse{200, "not json at all"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AlpacaTest, MissingFieldIsInternal) {
  http_.responses.push_back(
      HttpResponse{200, R"({"symbol":"AAPL","trade":{"t":"bad","s":1}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AlpacaTest, NonNumericPriceIsInternal) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"trade":{"t":"2026-07-10T19:59:58Z","p":"189.95","s":100}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("not a number")));
}

TEST_F(AlpacaTest, NonNumericVolumeIsInternal) {
  http_.responses.push_back(HttpResponse{
      200,
      R"({"bars":[{"t":"2026-01-02T05:00:00Z","o":1,"h":1,"l":1,"c":1,"v":"1000"}],
          "next_page_token":null})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetDailyBars(Sym("AAPL"), absl::CivilDay(2026, 1, 2),
                                    absl::CivilDay(2026, 1, 31)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("volume is not a number")));
}

TEST_F(AlpacaTest, NonStringTimestampIsInternal) {
  http_.responses.push_back(HttpResponse{
      200, R"({"trade":{"t":12345,"p":189.95,"s":100}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("timestamp is not a string")));
}

TEST_F(AlpacaTest, BadTimestampFormatIsInternal) {
  http_.responses.push_back(HttpResponse{
      200, R"({"trade":{"t":"not-a-time","p":189.95,"s":100}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("timestamp")));
}

TEST_F(AlpacaTest, TradeFieldNotAnObjectIsInternal) {
  http_.responses.push_back(HttpResponse{200, R"({"trade":"oops"})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("is not an object")));
}

TEST_F(AlpacaTest, TransportErrorPropagates) {
  http_.responses.push_back(absl::UnavailableError("connection refused"));
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(AlpacaTest, AbsurdPriceIsInternal) {
  http_.responses.push_back(HttpResponse{
      200, R"({"trade":{"t":"2026-07-10T19:59:58Z","p":1e300,"s":100}})"});
  AlpacaProvider provider = MakeProvider();
  EXPECT_THAT(provider.GetLatestTrade(Sym("AAPL")),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("out of range")));
}

}  // namespace
}  // namespace firefly
