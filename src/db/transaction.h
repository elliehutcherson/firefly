#ifndef FIREFLY_DB_TRANSACTION_H_
#define FIREFLY_DB_TRANSACTION_H_

#include "absl/status/status.h"
#include "src/db/sql_executor.h"

namespace firefly {

// One database transaction. Unless Commit() succeeds, destruction rolls the
// transaction back. A transaction is single-threaded and must not outlive the
// Db that created it.
class Transaction : public SqlExecutor {
 public:
  using SqlExecutor::Execute;
  using SqlExecutor::Query;

  ~Transaction() override = default;

  // Commits all statements. May be called exactly once; no statements may be
  // issued afterward.
  virtual absl::Status Commit() = 0;
};

}  // namespace firefly

#endif  // FIREFLY_DB_TRANSACTION_H_
