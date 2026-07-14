#include <memory>

#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "src/api/server.h"
#include "src/common/config.h"
#include "src/common/db.h"

int main() {
  absl::InitializeLog();

  firefly::Config config = firefly::Config::FromEnv();

  absl::StatusOr<std::unique_ptr<firefly::Db>> db =
      firefly::OpenDb(config.database_url);
  if (!db.ok()) {
    LOG(ERROR) << "failed to open database: " << db.status();
    LOG(ERROR) << "is it running? try: docker compose up -d db";
    return 1;
  }

  LOG(INFO) << "firefly listening on " << config.bind_address << ":"
            << config.port;
  firefly::Server server(config, db->get());
  server.Run();
  return 0;
}
