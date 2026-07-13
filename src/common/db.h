#ifndef FIREFLY_COMMON_DB_H_
#define FIREFLY_COMMON_DB_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace firefly {

// One query result row. Values arrive as text; NULL columns are nullopt.
struct Row {
  std::vector<std::optional<std::string>> columns;
};

using Rows = std::vector<Row>;

// Positional statement parameters ($1, $2, ...); nullopt binds SQL NULL.
using DbParams = std::vector<std::optional<std::string>>;

// Defined in db_pool.h; owns the pqxx connections. Forward-declared here so
// including db.h never pulls in pqxx headers.
class DbPool;

// Postgres access behind a fixed-size connection pool. This is the only
// module that touches libpqxx; its exceptions are translated to absl::Status
// here and never escape (see docs/STYLE.md). Thread-safe: calls block until
// a pooled connection is free.
class Db {
 public:
  static constexpr int kDefaultPoolSize = 4;

  // Connects eagerly (fails fast on a bad URL or unreachable server).
  static absl::StatusOr<std::unique_ptr<Db>> Open(
      const std::string& database_url, int pool_size = kDefaultPoolSize);

  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;
  ~Db();

  // Runs one statement in its own transaction and returns the result rows.
  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params = {});

  // Runs one statement in its own transaction; returns affected row count.
  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params = {});

  // Cheap connectivity check (SELECT 1).
  absl::Status Ping();

  // TODO: multi-statement transaction support (SELECT ... FOR UPDATE flows)
  // arrives with the trading module.

 private:
  explicit Db(std::unique_ptr<DbPool> pool);

  std::unique_ptr<DbPool> pool_;
};

}  // namespace firefly

#endif  // FIREFLY_COMMON_DB_H_
