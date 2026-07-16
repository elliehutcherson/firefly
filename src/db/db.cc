#include "src/db/db.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <pqxx/pqxx>

#include "absl/log/log.h"
#include "src/common/status_macros.h"
#include "src/db/connection_pool.h"

namespace firefly {
namespace {

// Returns the leased connection to the pool on destruction.
class Lease {
 public:
  Lease(ConnectionPool& pool, std::unique_ptr<pqxx::connection> conn)
      : pool_(pool), conn_(std::move(conn)) {}
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  ~Lease() { pool_.Release(std::move(conn_), broken_); }

  pqxx::connection& connection() { return *conn_; }
  void MarkBroken() { broken_ = true; }

 private:
  ConnectionPool& pool_;
  std::unique_ptr<pqxx::connection> conn_;
  bool broken_ = false;
};

absl::Status ExceptionStatus(const pqxx::sql_error& e) {
  // Map only states callers can handle meaningfully. Everything else is an
  // application/schema failure, not invalid client input.
  if (e.sqlstate() == "23505") {
    return absl::AlreadyExistsError("database value already exists");
  }
  if (e.sqlstate() == "40001" || e.sqlstate() == "40P01") {
    LOG(WARNING) << "retryable database transaction error (sqlstate "
                 << e.sqlstate() << "): " << e.what();
    return absl::AbortedError("database transaction must be retried");
  }
  if (e.sqlstate() == "57014") {
    LOG(WARNING) << "database operation canceled: " << e.what();
    return absl::DeadlineExceededError("database operation was canceled");
  }
  LOG(ERROR) << "database SQL error (sqlstate " << e.sqlstate()
             << "): " << e.what();
  return absl::InternalError("database operation failed");
}

Rows ToRows(const pqxx::result& result) {
  Rows rows;
  rows.reserve(result.size());
  for (const pqxx::row& pq_row : result) {
    Row row;
    row.columns.reserve(pq_row.size());
    for (const pqxx::field& field : pq_row) {
      if (field.is_null()) {
        row.columns.emplace_back(std::nullopt);
      } else {
        row.columns.emplace_back(field.view());
      }
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

pqxx::params ToParams(const DbParams& params) {
  pqxx::params pq_params;
  for (const std::optional<std::string>& param : params) {
    pq_params.append(param);
  }
  return pq_params;
}

class PqxxTransaction : public Transaction {
 public:
  PqxxTransaction(ConnectionPool& pool,
                  std::unique_ptr<pqxx::connection> conn)
      : lease_(pool, std::move(conn)), transaction_(lease_.connection()) {}

  using Transaction::Execute;
  using Transaction::Query;

  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override {
    ASSIGN_OR_RETURN(pqxx::result result, Exec(sql, params));
    return ToRows(result);
  }

  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override {
    ASSIGN_OR_RETURN(pqxx::result result, Exec(sql, params));
    return static_cast<int64_t>(result.affected_rows());
  }

  absl::Status Commit() override {
    if (finished_) {
      return absl::FailedPreconditionError("transaction is already finished");
    }
    finished_ = true;
    try {
      transaction_.commit();
      return absl::OkStatus();
    } catch (const pqxx::broken_connection& e) {
      lease_.MarkBroken();
      return absl::UnavailableError(e.what());
    } catch (const pqxx::sql_error& e) {
      return ExceptionStatus(e);
    } catch (const std::exception& e) {
      return absl::InternalError(e.what());
    }
  }

 private:
  absl::StatusOr<pqxx::result> Exec(const std::string& sql,
                                    const DbParams& params) {
    if (finished_) {
      return absl::FailedPreconditionError("transaction is already finished");
    }
    try {
      return transaction_.exec(sql, ToParams(params));
    } catch (const pqxx::broken_connection& e) {
      finished_ = true;
      lease_.MarkBroken();
      return absl::UnavailableError(e.what());
    } catch (const pqxx::sql_error& e) {
      finished_ = true;
      return ExceptionStatus(e);
    } catch (const std::exception& e) {
      finished_ = true;
      return absl::InternalError(e.what());
    }
  }

  Lease lease_;
  pqxx::work transaction_;
  bool finished_ = false;
};

// The pqxx-backed Db. Lives here so no other module sees pqxx types.
class PqxxDb : public Db {
 public:
  explicit PqxxDb(std::unique_ptr<ConnectionPool> pool)
      : pool_(std::move(pool)) {}

  using Db::Execute;
  using Db::Query;

  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override {
    ASSIGN_OR_RETURN(std::unique_ptr<Transaction> transaction, Begin());
    ASSIGN_OR_RETURN(Rows rows, transaction->Query(sql, params));
    RETURN_IF_ERROR(transaction->Commit());
    return rows;
  }

  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override {
    ASSIGN_OR_RETURN(std::unique_ptr<Transaction> transaction, Begin());
    ASSIGN_OR_RETURN(const int64_t affected,
                     transaction->Execute(sql, params));
    RETURN_IF_ERROR(transaction->Commit());
    return affected;
  }

  absl::Status Ping() override { return Query("SELECT 1").status(); }

  absl::StatusOr<std::unique_ptr<Transaction>> Begin() override {
    ASSIGN_OR_RETURN(std::unique_ptr<pqxx::connection> conn, pool_->Acquire());
    try {
      return std::make_unique<PqxxTransaction>(*pool_, std::move(conn));
    } catch (const pqxx::broken_connection& e) {
      return absl::UnavailableError(e.what());
    } catch (const std::exception& e) {
      return absl::InternalError(e.what());
    }
  }

 private:
  std::unique_ptr<ConnectionPool> pool_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<Db>> OpenDb(const std::string& database_url,
                                           int pool_size) {
  if (pool_size < 1) {
    return absl::InvalidArgumentError("pool_size must be at least 1");
  }
  auto db = std::make_unique<PqxxDb>(
      std::make_unique<ConnectionPool>(database_url, pool_size));
  RETURN_IF_ERROR(db->Ping());
  LOG(INFO) << "database pool ready (max " << pool_size << " connections)";
  return db;
}

}  // namespace firefly
