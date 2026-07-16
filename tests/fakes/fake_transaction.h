#ifndef FIREFLY_TESTS_FAKES_FAKE_TRANSACTION_H_
#define FIREFLY_TESTS_FAKES_FAKE_TRANSACTION_H_

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/db/transaction.h"
#include "tests/fakes/fake_db_types.h"

namespace firefly {

// Transaction view over FakeDb's canned results and recorded calls.
class FakeTransaction : public Transaction {
 public:
  FakeTransaction(std::vector<FakeDbCall>* calls,
                  std::deque<absl::StatusOr<Rows>>* query_results,
                  std::deque<absl::StatusOr<int64_t>>* execute_results,
                  int* commits)
      : calls_(calls),
        query_results_(query_results),
        execute_results_(execute_results),
        commits_(commits) {}

  using Transaction::Execute;
  using Transaction::Query;

  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override {
    if (finished_) {
      return absl::FailedPreconditionError("fake transaction is finished");
    }
    calls_->push_back({sql, params});
    if (query_results_->empty()) {
      return absl::InternalError("FakeTransaction: no query result left");
    }
    absl::StatusOr<Rows> result = std::move(query_results_->front());
    query_results_->pop_front();
    return result;
  }

  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override {
    if (finished_) {
      return absl::FailedPreconditionError("fake transaction is finished");
    }
    calls_->push_back({sql, params});
    if (execute_results_->empty()) {
      return absl::InternalError("FakeTransaction: no execute result left");
    }
    absl::StatusOr<int64_t> result = std::move(execute_results_->front());
    execute_results_->pop_front();
    return result;
  }

  absl::Status Commit() override {
    if (finished_) {
      return absl::FailedPreconditionError("fake transaction is finished");
    }
    finished_ = true;
    ++*commits_;
    return absl::OkStatus();
  }

 private:
  std::vector<FakeDbCall>* const calls_;
  std::deque<absl::StatusOr<Rows>>* const query_results_;
  std::deque<absl::StatusOr<int64_t>>* const execute_results_;
  int* const commits_;
  bool finished_ = false;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_TRANSACTION_H_
