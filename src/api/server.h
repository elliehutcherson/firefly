#ifndef FIREFLY_API_SERVER_H_
#define FIREFLY_API_SERVER_H_

#include "src/common/config.h"
#include "src/common/db.h"

namespace firefly {

// Outcome of the health probe, independent of HTTP framing. Kept crow-free
// so it can be unit-tested with a fake Db.
struct HealthReport {
  int http_status = 0;
  bool db_ok = false;
};

// Pings the database and maps the result to a health report.
HealthReport CheckHealth(Db& db);

class Server {
 public:
  // `db` is borrowed and must outlive the server.
  Server(Config config, Db* db);

  // Registers routes and runs the HTTP server. Blocks until shutdown.
  void Run();

 private:
  Config config_;
  Db* db_;
};

}  // namespace firefly

#endif  // FIREFLY_API_SERVER_H_
