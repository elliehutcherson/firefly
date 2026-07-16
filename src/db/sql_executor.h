#ifndef FIREFLY_DB_SQL_EXECUTOR_H_
#define FIREFLY_DB_SQL_EXECUTOR_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "src/db/db_types.h"

namespace firefly {

// Executes SQL against either the database's single-statement path or an
// already-open transaction.
class SqlExecutor {
 public:
  virtual ~SqlExecutor() = default;

  virtual absl::StatusOr<Rows> Query(const std::string& sql,
                                     const DbParams& params) = 0;
  virtual absl::StatusOr<int64_t> Execute(const std::string& sql,
                                          const DbParams& params) = 0;

  // Parameterless conveniences; overloads rather than default arguments
  // because defaults are banned on virtual functions.
  absl::StatusOr<Rows> Query(const std::string& sql) { return Query(sql, {}); }
  absl::StatusOr<int64_t> Execute(const std::string& sql) {
    return Execute(sql, {});
  }
};

}  // namespace firefly

#endif  // FIREFLY_DB_SQL_EXECUTOR_H_
