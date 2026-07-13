#include "src/api/server.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "crow.h"  // IWYU pragma: keep
#include "nlohmann/json.hpp"

namespace firefly {
namespace {

constexpr int kStatusOk = 200;
constexpr int kStatusServiceUnavailable = 503;

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

crow::response Healthz(Db& db) {
  absl::Status db_status = db.Ping();
  if (!db_status.ok()) {
    LOG(WARNING) << "healthz: db ping failed: " << db_status;
    return JsonResponse(kStatusServiceUnavailable,
                        {{"status", "degraded"}, {"db", "unavailable"}});
  }
  return JsonResponse(kStatusOk, {{"status", "ok"}, {"db", "ok"}});
}

crow::response Ping() { return JsonResponse(kStatusOk, {{"pong", true}}); }

}  // namespace

Server::Server(Config config, Db* db) : config_(std::move(config)), db_(db) {}

void Server::Run() {
  static CrowLogBridge log_bridge;
  crow::logger::setHandler(&log_bridge);

  crow::SimpleApp app;
  app.loglevel(crow::LogLevel::Warning);

  CROW_ROUTE(app, "/healthz")([this] { return Healthz(*db_); });
  CROW_ROUTE(app, "/api/v1/ping")([] { return Ping(); });

  app.bindaddr(config_.bind_address)
      .port(static_cast<std::uint16_t>(config_.port))
      .multithreaded()
      .run();
}

}  // namespace firefly
