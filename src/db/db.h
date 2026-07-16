#ifndef FIREFLY_DB_DB_H_
#define FIREFLY_DB_DB_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "src/db/sql_executor.h"
#include "src/db/transaction.h"

namespace firefly {

// Postgres access behind a project-owned interface; this is the single place
// to fake in tests. The pqxx-backed implementation lives in db.cc — the only
// module that touches libpqxx; its exceptions are translated to absl::Status
// there and never escape (see docs/STYLE.md).
class Db : public SqlExecutor {
 public:
  using SqlExecutor::Execute;
  using SqlExecutor::Query;

  ~Db() override = default;

  // Runs one statement in its own transaction and returns the result rows.
  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override = 0;

  // Runs one statement in its own transaction; returns affected row count.
  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override = 0;

  // Cheap connectivity check (SELECT 1).
  virtual absl::Status Ping() = 0;

  // Starts a multi-statement transaction on one leased connection. The caller
  // owns the returned transaction and must Commit() it explicitly; otherwise
  // destruction rolls it back.
  virtual absl::StatusOr<std::unique_ptr<Transaction>> Begin() = 0;
};

struct DbOptions {
  int pool_size = 4;
  absl::Duration acquire_timeout = absl::Seconds(2);
};

// Production Db backed by a fixed-size libpqxx connection pool. Connects
// eagerly (fails fast on a bad URL or unreachable server). Thread-safe.
// Calls return DeadlineExceeded if the pool remains exhausted for the
// configured acquisition timeout.
absl::StatusOr<std::unique_ptr<Db>> OpenDb(
    const std::string& database_url, DbOptions options = {});

}  // namespace firefly

#endif  // FIREFLY_DB_DB_H_
