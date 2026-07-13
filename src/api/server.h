#ifndef FIREFLY_API_SERVER_H_
#define FIREFLY_API_SERVER_H_

#include "src/common/config.h"
#include "src/common/db.h"

namespace firefly {

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
