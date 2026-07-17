#ifndef FIREFLY_DB_CONNECTION_POOL_H_
#define FIREFLY_DB_CONNECTION_POOL_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <pqxx/connection>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace firefly {

// Fixed-size lazy connection pool: connections are created on demand up to
// `max_size`, then callers block until one is released.
//
// Internal to the Db wrapper — this header names pqxx types, and pqxx must
// not leak past backend/src/db/* (see docs/STYLE.md). Include it only from the
// database implementation.
class ConnectionPool {
 public:
  ConnectionPool(std::string url, int max_size,
                 absl::Duration acquire_timeout);

  // Waits up to `acquire_timeout` for a pooled connection or an open slot.
  // Returns DeadlineExceeded when the pool remains exhausted.
  absl::StatusOr<std::unique_ptr<pqxx::connection>> Acquire();

  // Returns a connection to the pool. Broken (or closed) connections are
  // dropped instead, so a later Acquire dials a fresh one.
  void Release(std::unique_ptr<pqxx::connection> conn, bool broken);

 private:
  const std::string url_;
  const int max_size_;
  const absl::Duration acquire_timeout_;

  std::mutex mu_;
  std::condition_variable cv_;
  int open_count_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::unique_ptr<pqxx::connection>> idle_ ABSL_GUARDED_BY(mu_);
};

}  // namespace firefly

#endif  // FIREFLY_DB_CONNECTION_POOL_H_
