#include "src/db/connection_pool.h"

#include <memory>
#include <pqxx/pqxx>
#include <utility>

#include "absl/strings/str_cat.h"

namespace firefly {

ConnectionPool::ConnectionPool(std::string url, int max_size)
    : url_(std::move(url)), max_size_(max_size) {}

absl::StatusOr<std::unique_ptr<pqxx::connection>> ConnectionPool::Acquire() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock,
             [this] { return !idle_.empty() || open_count_ < max_size_; });
    if (!idle_.empty()) {
      auto conn = std::move(idle_.back());
      idle_.pop_back();
      return conn;
    }
    ++open_count_;  // Reserve a slot; dial outside the lock.
  }
  try {
    return std::make_unique<pqxx::connection>(url_);
  } catch (const std::exception& e) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      --open_count_;
    }
    cv_.notify_one();
    return absl::UnavailableError(
        absl::StrCat("connecting to database: ", e.what()));
  }
}

void ConnectionPool::Release(std::unique_ptr<pqxx::connection> conn,
                             bool broken) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (broken || conn == nullptr || !conn->is_open()) {
      --open_count_;
    } else {
      idle_.push_back(std::move(conn));
    }
  }
  cv_.notify_one();
}

}  // namespace firefly
