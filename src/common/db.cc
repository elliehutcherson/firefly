#include "src/common/db.h"

#include <pqxx/pqxx>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/common/db_pool.h"
#include "src/common/status_macros.h"

namespace firefly {
namespace {

// Returns the leased connection to the pool on destruction.
class Lease {
 public:
  Lease(DbPool& pool, std::unique_ptr<pqxx::connection> conn)
      : pool_(pool), conn_(std::move(conn)) {}
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  ~Lease() { pool_.Release(std::move(conn_), broken_); }

  pqxx::connection& connection() { return *conn_; }
  void MarkBroken() { broken_ = true; }

 private:
  DbPool& pool_;
  std::unique_ptr<pqxx::connection> conn_;
  bool broken_ = false;
};

// Runs one statement in its own transaction, translating pqxx exceptions
// into a Status. The only try/catch over query execution in the project.
absl::StatusOr<pqxx::result> Exec(DbPool& pool, const std::string& sql,
                                  const DbParams& params) {
  ASSIGN_OR_RETURN(std::unique_ptr<pqxx::connection> conn, pool.Acquire());
  Lease lease(pool, std::move(conn));
  try {
    pqxx::work txn(lease.connection());
    pqxx::params pq_params;
    for (const std::optional<std::string>& param : params) {
      pq_params.append(param);
    }
    pqxx::result result = txn.exec(sql, pq_params);
    txn.commit();
    return result;
  } catch (const pqxx::broken_connection& e) {
    lease.MarkBroken();
    return absl::UnavailableError(e.what());
  } catch (const pqxx::sql_error& e) {
    // 23505 unique_violation: the row already exists (e.g. duplicate
    // username); callers map AlreadyExists to HTTP 409.
    if (e.sqlstate() == "23505") {
      return absl::AlreadyExistsError(
          absl::StrCat(e.what(), " [sqlstate 23505]"));
    }
    return absl::InvalidArgumentError(
        absl::StrCat(e.what(), " [sqlstate ", e.sqlstate(), "]"));
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  }
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

// The pqxx-backed Db. Lives here so no other module sees pqxx types.
class PqxxDb : public Db {
 public:
  explicit PqxxDb(std::unique_ptr<DbPool> pool) : pool_(std::move(pool)) {}

  using Db::Execute;
  using Db::Query;

  absl::StatusOr<Rows> Query(const std::string& sql,
                             const DbParams& params) override {
    ASSIGN_OR_RETURN(pqxx::result result, Exec(*pool_, sql, params));
    return ToRows(result);
  }

  absl::StatusOr<int64_t> Execute(const std::string& sql,
                                  const DbParams& params) override {
    ASSIGN_OR_RETURN(pqxx::result result, Exec(*pool_, sql, params));
    return static_cast<int64_t>(result.affected_rows());
  }

  absl::Status Ping() override { return Query("SELECT 1").status(); }

 private:
  std::unique_ptr<DbPool> pool_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<Db>> OpenDb(const std::string& database_url,
                                           int pool_size) {
  if (pool_size < 1) {
    return absl::InvalidArgumentError("pool_size must be at least 1");
  }
  auto db = std::make_unique<PqxxDb>(
      std::make_unique<DbPool>(database_url, pool_size));
  RETURN_IF_ERROR(db->Ping());
  LOG(INFO) << "database pool ready (max " << pool_size << " connections)";
  return db;
}

}  // namespace firefly
