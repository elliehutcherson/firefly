#ifndef FIREFLY_TESTS_FAKES_FAKE_DB_H_
#define FIREFLY_TESTS_FAKES_FAKE_DB_H_

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/common/db.h"

namespace firefly {

// One statement recorded by FakeDb.
struct FakeDbCall {
  std::string sql;
  DbParams params;
};

// Replays canned results and records every statement it receives.
class FakeDb : public Db {
 public:
  using Db::Execute;
  using Db::Query;

  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override {
    calls.push_back({sql, params});
    if (query_results.empty()) {
      return absl::InternalError("FakeDb: no canned query result left");
    }
    absl::StatusOr<Rows> result = std::move(query_results.front());
    query_results.pop_front();
    return result;
  }

  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override {
    calls.push_back({sql, params});
    if (execute_results.empty()) {
      return absl::InternalError("FakeDb: no canned execute result left");
    }
    absl::StatusOr<int64_t> result = std::move(execute_results.front());
    execute_results.pop_front();
    return result;
  }

  absl::Status Ping() override { return ping_status; }

  std::vector<FakeDbCall> calls;
  std::deque<absl::StatusOr<Rows>> query_results;
  std::deque<absl::StatusOr<int64_t>> execute_results;
  absl::Status ping_status;  // OK unless a test sets an outage.
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_DB_H_
