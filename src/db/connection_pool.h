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

namespace firefly {

// Fixed-size lazy connection pool: connections are created on demand up to
// `max_size`, then callers block until one is released.
//
// Internal to the Db wrapper — this header names pqxx types, and pqxx must
// not leak past src/db/* (see docs/STYLE.md). Include it only from the
// database implementation.
class ConnectionPool {
 public:
  explicit ConnectionPool(std::string url, int max_size);

  // Blocks until a pooled connection is free or a slot opens, then hands out
  // a pooled or freshly dialed connection.
  absl::StatusOr<std::unique_ptr<pqxx::connection>> Acquire();

  // Returns a connection to the pool. Broken (or closed) connections are
  // dropped instead, so a later Acquire dials a fresh one.
  void Release(std::unique_ptr<pqxx::connection> conn, bool broken);

 private:
  const std::string url_;
  const int max_size_;

  std::mutex mu_;
  std::condition_variable cv_;
  int open_count_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::unique_ptr<pqxx::connection>> idle_ ABSL_GUARDED_BY(mu_);
};

}  // namespace firefly

#endif  // FIREFLY_DB_CONNECTION_POOL_H_
