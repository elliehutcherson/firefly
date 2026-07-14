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

// Postgres access behind a project-owned interface; this is the single place
// to fake in tests. The pqxx-backed implementation lives in db.cc — the only
// module that touches libpqxx; its exceptions are translated to absl::Status
// there and never escape (see docs/STYLE.md).
class Db {
 public:
  static constexpr int kDefaultPoolSize = 4;

  virtual ~Db() = default;

  // Runs one statement in its own transaction and returns the result rows.
  virtual absl::StatusOr<Rows> Query(const std::string& sql,
                                     const DbParams& params) = 0;

  // Runs one statement in its own transaction; returns affected row count.
  virtual absl::StatusOr<int64_t> Execute(const std::string& sql,
                                          const DbParams& params) = 0;

  // Cheap connectivity check (SELECT 1).
  virtual absl::Status Ping() = 0;

  // Parameterless conveniences; overloads rather than default arguments
  // because defaults are banned on virtual functions.
  absl::StatusOr<Rows> Query(const std::string& sql) { return Query(sql, {}); }
  absl::StatusOr<int64_t> Execute(const std::string& sql) {
    return Execute(sql, {});
  }

  // TODO: multi-statement transaction support (SELECT ... FOR UPDATE flows)
  // arrives with the trading module.
};

// Production Db backed by a fixed-size libpqxx connection pool. Connects
// eagerly (fails fast on a bad URL or unreachable server). Thread-safe:
// calls block until a pooled connection is free.
absl::StatusOr<std::unique_ptr<Db>> OpenDb(
    const std::string& database_url, int pool_size = Db::kDefaultPoolSize);

}  // namespace firefly

#endif  // FIREFLY_COMMON_DB_H_
