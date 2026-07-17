#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "src/common/config.h"
#include "src/common/http.h"
#include "src/db/db.h"
#include "tests/support/status_matchers.h"

// The Milestone 4 verification flow, automated: spawn the real firefly
// binary (FIREFLY_SERVER_BINARY, injected by CMake), drive its public HTTP
// interface as a browser would — session cookie and all — and check the
// ledger both through the API and directly in Postgres.
//
// Prices come from Alpaca's real quote, so assertions check relationships
// (cash == $10,000 + every delta; a round trip never mints), never absolute
// prices. The closed-market bypass is set so the flow runs at any hour.
//
// Skips itself without Alpaca credentials in the environment or a reachable
// database (run with: set -a; source .env; set +a; RUN_E2E=1 ...).

namespace firefly {
namespace {

using ::nlohmann::json;

constexpr char kJsonType[] = "application/json";

json MustParse(const std::string& body) {
  json parsed = json::parse(body, /*cb=*/nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(parsed.is_discarded()) << "not JSON: " << body;
  return parsed;
}

class TradingFlowE2eTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (std::getenv("APCA_API_KEY_ID") == nullptr ||
        std::getenv("APCA_API_SECRET_KEY") == nullptr) {
      GTEST_SKIP() << "APCA_API_KEY_ID / APCA_API_SECRET_KEY not set";
    }
    absl::StatusOr<std::unique_ptr<Db>> db =
        OpenDb(Config::FromEnv().database_url, {.pool_size = 1});
    if (!db.ok()) {
      GTEST_SKIP() << "database unavailable: " << db.status();
    }
    db_ = std::move(*db);
    http_ = CreateHttpClient();
    port_ = 18000 + (getpid() % 1000);
    base_url_ = absl::StrCat("http://127.0.0.1:", port_);
    server_log_ = absl::StrCat(::testing::TempDir(), "firefly_e2e_", port_,
                               ".log");
    SpawnServer();
    WaitForHealthy();
  }

  void TearDown() override {
    if (server_pid_ > 0) {
      kill(server_pid_, SIGTERM);
      waitpid(server_pid_, nullptr, 0);
    }
    if (db_ != nullptr && user_id_ > 0) {
      const DbParams params = {absl::StrCat(user_id_)};
      EXPECT_OK(db_->Execute("DELETE FROM orders WHERE user_id = $1", params));
      EXPECT_OK(
          db_->Execute("DELETE FROM positions WHERE user_id = $1", params));
      EXPECT_OK(
          db_->Execute("DELETE FROM sessions WHERE user_id = $1", params));
      EXPECT_OK(db_->Execute("DELETE FROM users WHERE id = $1", params));
    }
  }

  void SpawnServer() {
    server_pid_ = fork();
    ASSERT_NE(server_pid_, -1) << "fork failed";
    if (server_pid_ != 0) {
      return;
    }
    // Child. Quiet the server's logs into a file gtest can show on failure.
    setenv("FIREFLY_PORT", absl::StrCat(port_).c_str(), /*overwrite=*/1);
    setenv("FIREFLY_ALLOW_CLOSED_MARKET_TRADING", "1", /*overwrite=*/1);
    if (std::freopen(server_log_.c_str(), "w", stdout) == nullptr ||
        std::freopen(server_log_.c_str(), "a", stderr) == nullptr) {
      _exit(126);
    }
    execl(FIREFLY_SERVER_BINARY, "firefly", static_cast<char*>(nullptr));
    _exit(127);  // exec only returns on failure.
  }

  void WaitForHealthy() {
    for (int attempt = 0; attempt < 30; ++attempt) {
      const absl::StatusOr<HttpResponse> health =
          http_->Get({.url = absl::StrCat(base_url_, "/healthz")});
      if (health.ok() && health->status_code == 200) {
        return;
      }
      if (waitpid(server_pid_, nullptr, WNOHANG) != 0) {
        server_pid_ = 0;
        GTEST_SKIP() << "server exited during startup; log: " << server_log_;
      }
      absl::SleepFor(absl::Milliseconds(500));
    }
    GTEST_SKIP() << "server never became healthy; log: " << server_log_;
  }

  // GET/POST against the server, sending the captured session cookie and
  // keeping any newly set one (exactly what a browser does with ff_session).
  absl::StatusOr<HttpResponse> Get(const std::string& path) {
    return KeepSession(http_->Get(WithCookie(path)));
  }
  absl::StatusOr<HttpResponse> Post(const std::string& path,
                                    const json& body) {
    HttpRequest request = WithCookie(path);
    request.body = body.dump();
    return KeepSession(http_->Post(request));
  }

  std::unique_ptr<Db> db_;
  std::unique_ptr<HttpClient> http_;
  int port_ = 0;
  std::string base_url_;
  std::string server_log_;
  pid_t server_pid_ = 0;
  int64_t user_id_ = 0;     // Set after signup; drives TearDown cleanup.
  std::string session_cookie_;

 private:
  HttpRequest WithCookie(const std::string& path) {
    HttpRequest request{.url = absl::StrCat(base_url_, path)};
    if (!session_cookie_.empty()) {
      request.headers.emplace_back(
          "Cookie", absl::StrCat("ff_session=", session_cookie_));
    }
    return request;
  }

  absl::StatusOr<HttpResponse> KeepSession(
      absl::StatusOr<HttpResponse> response) {
    if (response.ok()) {
      for (const auto& [name, value] : response->cookies) {
        if (name == "ff_session" && !value.empty()) {
          session_cookie_ = value;
        }
      }
    }
    return response;
  }
};

TEST_F(TradingFlowE2eTest, FullFlowThroughThePublicInterface) {
  // Signup issues the session cookie every later request rides on. The
  // username must fit the 20-character cap, so keep ten digits of the clock.
  const std::string username = absl::StrCat(
      "e2e_probe_", absl::ToUnixMicros(absl::Now()) % 10000000000);
  absl::StatusOr<HttpResponse> signup = Post(
      "/api/v1/auth/signup", {{"username", username}, {"password", "hunter2222"}});
  ASSERT_OK(signup);
  ASSERT_EQ(signup->status_code, 200) << signup->body;
  user_id_ = MustParse(signup->body)["user_id"].get<int64_t>();
  ASSERT_FALSE(session_cookie_.empty()) << "signup set no ff_session cookie";

  // A fresh account holds exactly $10,000.
  absl::StatusOr<HttpResponse> me = Get("/api/v1/me");
  ASSERT_OK(me);
  ASSERT_EQ(me->status_code, 200) << me->body;
  EXPECT_EQ(MustParse(me->body)["cash_cents"], 1000000);

  // Buy 2 AAPL at the live quote: negative ceil'd debit, cash arithmetic
  // exact against the returned delta.
  absl::StatusOr<HttpResponse> buy = Post(
      "/api/v1/orders", {{"symbol", "AAPL"}, {"side", "buy"}, {"quantity", 2}});
  ASSERT_OK(buy);
  ASSERT_EQ(buy->status_code, 200) << buy->body;
  const json buy_body = MustParse(buy->body);
  const int64_t buy_delta = buy_body["cash_delta_cents"].get<int64_t>();
  const int64_t cash_after_buy = buy_body["cash_cents"].get<int64_t>();
  EXPECT_LT(buy_delta, 0);
  EXPECT_EQ(cash_after_buy, 1000000 + buy_delta);
  EXPECT_EQ(buy_body["position"]["quantity"], 2);

  absl::StatusOr<HttpResponse> portfolio = Get("/api/v1/portfolio");
  ASSERT_OK(portfolio);
  ASSERT_EQ(portfolio->status_code, 200) << portfolio->body;
  const json with_position = MustParse(portfolio->body);
  EXPECT_EQ(with_position["cash_cents"], cash_after_buy);
  ASSERT_EQ(with_position["positions"].size(), 1);
  EXPECT_EQ(with_position["positions"][0]["symbol"], "AAPL");

  // Crossing zero is rejected as account state, not request shape.
  absl::StatusOr<HttpResponse> crossing = Post(
      "/api/v1/orders", {{"symbol", "AAPL"}, {"side", "sell"}, {"quantity", 5}});
  ASSERT_OK(crossing);
  EXPECT_EQ(crossing->status_code, 422) << crossing->body;
  EXPECT_THAT(crossing->body, ::testing::HasSubstr("exceeds shares held"));

  // Sell to flat: positive floor'd credit, position row gone.
  absl::StatusOr<HttpResponse> sell = Post(
      "/api/v1/orders", {{"symbol", "AAPL"}, {"side", "sell"}, {"quantity", 2}});
  ASSERT_OK(sell);
  ASSERT_EQ(sell->status_code, 200) << sell->body;
  const json sell_body = MustParse(sell->body);
  const int64_t sell_delta = sell_body["cash_delta_cents"].get<int64_t>();
  EXPECT_GT(sell_delta, 0);
  EXPECT_EQ(sell_body["cash_cents"], cash_after_buy + sell_delta);
  EXPECT_TRUE(sell_body["position"].is_null());

  absl::StatusOr<HttpResponse> unknown = Post(
      "/api/v1/orders", {{"symbol", "ZZZZ"}, {"side", "buy"}, {"quantity", 1}});
  ASSERT_OK(unknown);
  EXPECT_EQ(unknown->status_code, 404) << unknown->body;

  absl::StatusOr<HttpResponse> cover = Post(
      "/api/v1/orders", {{"symbol", "AAPL"}, {"side", "cover"}, {"quantity", 1}});
  ASSERT_OK(cover);
  EXPECT_EQ(cover->status_code, 422) << cover->body;

  // Without the cookie the order never reaches the store.
  const std::string cookie = std::exchange(session_cookie_, "");
  absl::StatusOr<HttpResponse> anonymous = Post(
      "/api/v1/orders", {{"symbol", "AAPL"}, {"side", "buy"}, {"quantity", 1}});
  ASSERT_OK(anonymous);
  EXPECT_EQ(anonymous->status_code, 401) << anonymous->body;
  session_cookie_ = cookie;

  // The conserved invariant through the public interface: final cash is the
  // starting $10,000 plus every executed delta, the account is flat, and
  // the ceil/floor asymmetry means the round trip never minted cash.
  absl::StatusOr<HttpResponse> final_portfolio = Get("/api/v1/portfolio");
  ASSERT_OK(final_portfolio);
  const json flat = MustParse(final_portfolio->body);
  EXPECT_EQ(flat["cash_cents"], 1000000 + buy_delta + sell_delta);
  EXPECT_TRUE(flat["positions"].empty());
  EXPECT_LE(buy_delta + sell_delta, 0);

  // And the same invariant straight from Postgres.
  absl::StatusOr<Rows> ledger = db_->Query(
      "SELECT (cash_cents = 1000000 + (SELECT coalesce(sum(cash_delta_cents),"
      " 0) FROM orders WHERE user_id = $1)) FROM users WHERE id = $1",
      {absl::StrCat(user_id_)});
  ASSERT_OK(ledger);
  ASSERT_EQ(ledger->size(), 1);
  EXPECT_EQ((*ledger)[0].columns[0], "t");
}

}  // namespace
}  // namespace firefly
