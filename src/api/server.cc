#include "src/api/server.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "crow.h"  // IWYU pragma: keep
#include "crow/middlewares/cookie_parser.h"
#include "nlohmann/json.hpp"
#include "src/api/auth_handlers.h"
#include "src/api/market_handlers.h"
#include "src/api/rate_limiter.h"

namespace firefly {
namespace {

constexpr int kStatusOk = 200;
constexpr int kStatusTooManyRequests = 429;
constexpr int kStatusServiceUnavailable = 503;

constexpr char kSessionCookie[] = "ff_session";

// Market-data responses are public and edge-cached; Cloudflare absorbs most
// read traffic at this max-age. Errors must not be pinned at the edge.
constexpr char kCacheable[] = "public, max-age=60";
constexpr char kUncacheable[] = "no-store";

// Routes Crow's internal log lines into abseil logging (see docs/STYLE.md).
class CrowLogBridge : public crow::ILogHandler {
 public:
  void log(const std::string& message, crow::LogLevel level) override {
    switch (level) {
      case crow::LogLevel::Critical:
      case crow::LogLevel::Error:
        LOG(ERROR) << "crow: " << message;
        break;
      case crow::LogLevel::Warning:
        LOG(WARNING) << "crow: " << message;
        break;
      default:
        LOG(INFO) << "crow: " << message;
        break;
    }
  }
};

crow::response JsonResponse(int code, const nlohmann::json& body) {
  crow::response response(code, body.dump());
  response.set_header("Content-Type", "application/json");
  return response;
}

// The real client address: the socket peer in dev, or a shape-checked
// trusted proxy header (CF-Connecting-IP) in prod. Only safe because Crow
// binds localhost behind Caddy/Cloudflare, which always set that header.
std::optional<std::string> ResolveClientIp(const crow::request& req,
                                           const std::string& header_name) {
  if (header_name.empty()) {
    return SanitizeClientIp(req.remote_ip_address);
  }
  std::optional<std::string> forwarded =
      SanitizeClientIp(req.get_header_value(header_name));
  if (forwarded.has_value()) {
    return forwarded;
  }
  return SanitizeClientIp(req.remote_ip_address);
}

// Applies the token-bucket limiter to the auth endpoints only; everything
// else is public reads absorbed by the edge cache. Members are wired
// post-construction via app.get_middleware<>() — Crow middlewares cannot
// take constructor arguments through the app template.
struct RateLimitMiddleware {
  struct context {};

  void before_handle(crow::request& req, crow::response& res, context&) {
    if (limiter == nullptr || req.method != crow::HTTPMethod::Post ||
        !absl::StartsWith(req.url, "/api/v1/auth/")) {
      return;
    }
    const std::optional<std::string> ip =
        ResolveClientIp(req, client_ip_header);
    if (limiter->Allow(absl::StrCat("ip:", ip.value_or("unknown")))) {
      return;
    }
    res = JsonResponse(kStatusTooManyRequests,
                       {{"error", "too many requests"}});
    res.set_header("Cache-Control", "no-store");
    res.end();
  }

  void after_handle(crow::request&, crow::response&, context&) {}

  TokenBucketRateLimiter* limiter = nullptr;
  std::string client_ip_header;
};

using FireflyApp = crow::App<crow::CookieParser, RateLimitMiddleware>;

// HTTP framing for an auth core's result: always no-store, and the cookie
// intents (set/clear ff_session) are applied to the CookieParser context —
// cores never see cookies.
crow::response AuthResponse(const absl::StatusOr<AuthResult>& result,
                            crow::CookieParser::context& cookies,
                            absl::Duration session_ttl) {
  if (!result.ok()) {
    crow::response response =
        JsonResponse(HttpStatusFromCode(result.status().code()),
                     {{"error", std::string(result.status().message())}});
    response.set_header("Cache-Control", "no-store");
    return response;
  }
  if (!result->set_session_token.empty()) {
    cookies.set_cookie(kSessionCookie, result->set_session_token)
        .httponly()
        .secure()
        .same_site(crow::CookieParser::Cookie::SameSitePolicy::Lax)
        .path("/")
        .max_age(absl::ToInt64Seconds(session_ttl));
  } else if (result->clear_session) {
    cookies.set_cookie(kSessionCookie, "")
        .httponly()
        .secure()
        .same_site(crow::CookieParser::Cookie::SameSitePolicy::Lax)
        .path("/")
        .max_age(0);
  }
  crow::response response = JsonResponse(kStatusOk, result->body);
  response.set_header("Cache-Control", "no-store");
  return response;
}

// Same framing for session-read endpoints (/me): no cookie changes.
crow::response NoStoreResponse(const absl::StatusOr<nlohmann::json>& result) {
  crow::response response =
      result.ok()
          ? JsonResponse(kStatusOk, *result)
          : JsonResponse(HttpStatusFromCode(result.status().code()),
                         {{"error", std::string(result.status().message())}});
  response.set_header("Cache-Control", "no-store");
  return response;
}

// HTTP framing for a market handler core's result.
crow::response MarketResponse(const absl::StatusOr<nlohmann::json>& result) {
  if (!result.ok()) {
    crow::response response =
        JsonResponse(HttpStatusFromCode(result.status().code()),
                     {{"error", std::string(result.status().message())}});
    response.set_header("Cache-Control", kUncacheable);
    return response;
  }
  crow::response response = JsonResponse(kStatusOk, *result);
  response.set_header("Cache-Control", kCacheable);
  return response;
}

crow::response Healthz(Db& db) {
  const HealthReport report = CheckHealth(db);
  if (!report.db_ok) {
    return JsonResponse(report.http_status,
                        {{"status", "degraded"}, {"db", "unavailable"}});
  }
  return JsonResponse(report.http_status, {{"status", "ok"}, {"db", "ok"}});
}

crow::response Ping() { return JsonResponse(kStatusOk, {{"pong", true}}); }

}  // namespace

HealthReport CheckHealth(Db& db) {
  absl::Status db_status = db.Ping();
  if (!db_status.ok()) {
    LOG(WARNING) << "healthz: db ping failed: " << db_status;
    return {.http_status = kStatusServiceUnavailable, .db_ok = false};
  }
  return {.http_status = kStatusOk, .db_ok = true};
}

Server::Server(Config config, ServerDeps deps)
    : config_(std::move(config)), deps_(deps) {}

void Server::Run() {
  static CrowLogBridge log_bridge;
  crow::logger::setHandler(&log_bridge);

  FireflyApp app;
  app.loglevel(crow::LogLevel::Warning);

  TokenBucketRateLimiter limiter(
      deps_.auth.clock,
      {.tokens_per_second = config_.auth_rate_per_min / 60.0,
       .burst = config_.auth_burst});
  app.get_middleware<RateLimitMiddleware>().limiter = &limiter;
  app.get_middleware<RateLimitMiddleware>().client_ip_header =
      config_.client_ip_header;

  CROW_ROUTE(app, "/healthz")([this] { return Healthz(*deps_.db); });
  CROW_ROUTE(app, "/api/v1/ping")([] { return Ping(); });

  const absl::Duration session_ttl = deps_.auth.session_ttl;
  CROW_ROUTE(app, "/api/v1/auth/signup")
      .methods(crow::HTTPMethod::Post)([this, &app,
                                        session_ttl](const crow::request& req) {
        return AuthResponse(
            Signup(deps_.auth, req.body,
                   ResolveClientIp(req, config_.client_ip_header)),
            app.get_context<crow::CookieParser>(req), session_ttl);
      });
  CROW_ROUTE(app, "/api/v1/auth/login")
      .methods(crow::HTTPMethod::Post)([this, &app,
                                        session_ttl](const crow::request& req) {
        return AuthResponse(
            Login(deps_.auth, req.body,
                  ResolveClientIp(req, config_.client_ip_header)),
            app.get_context<crow::CookieParser>(req), session_ttl);
      });
  CROW_ROUTE(app, "/api/v1/auth/logout")
      .methods(crow::HTTPMethod::Post)([this, &app,
                                        session_ttl](const crow::request& req) {
        auto& cookies = app.get_context<crow::CookieParser>(req);
        return AuthResponse(
            Logout(deps_.auth, cookies.get_cookie(kSessionCookie)), cookies,
            session_ttl);
      });
  CROW_ROUTE(app, "/api/v1/me")
  ([this, &app](const crow::request& req) {
    return NoStoreResponse(GetMeJson(
        deps_.auth,
        app.get_context<crow::CookieParser>(req).get_cookie(kSessionCookie)));
  });

  CROW_ROUTE(app, "/api/v1/quote/<string>")
  ([this](const std::string& symbol) {
    return MarketResponse(GetQuoteJson(deps_.market, symbol));
  });
  CROW_ROUTE(app, "/api/v1/candles/<string>/daily")
  ([this](const crow::request& req, const std::string& symbol) {
    const char* range = req.url_params.get("range");
    return MarketResponse(GetDailyCandlesJson(
        deps_.market, symbol, range == nullptr ? "year" : range));
  });
  CROW_ROUTE(app, "/api/v1/candles/<string>/intraday")
  ([this](const std::string& symbol) {
    return MarketResponse(GetIntradayCandlesJson(deps_.market, symbol));
  });

  app.bindaddr(config_.bind_address)
      .port(static_cast<std::uint16_t>(config_.port))
      .multithreaded()
      .run();
}

}  // namespace firefly
