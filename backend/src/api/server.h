#ifndef FIREFLY_API_SERVER_H_
#define FIREFLY_API_SERVER_H_

#include "api/auth_handlers.h"
#include "api/market_handlers.h"
#include "api/trading_handlers.h"
#include "common/config.h"
#include "db/db.h"

namespace firefly {

// Outcome of the health probe, independent of HTTP framing. Kept crow-free
// so it can be unit-tested with a fake Db.
struct HealthReport {
  int http_status = 0;
  bool db_ok = false;
};

// Pings the database and maps the result to a health report.
HealthReport CheckHealth(Db& db);

// Everything the routes need; all borrowed, must outlive the server.
struct ServerDeps {
  Db& db;
  MarketDeps market;
  AuthDeps auth;
  TradingDeps trading;
};

class Server {
 public:
  Server(Config config, ServerDeps deps);

  // Registers routes and runs the HTTP server. Blocks until shutdown.
  void Run();

 private:
  Config config_;
  ServerDeps deps_;
};

}  // namespace firefly

#endif  // FIREFLY_API_SERVER_H_
