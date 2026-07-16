#include "src/db/connection_pool.h"

#include <chrono>
#include <memory>
#include <pqxx/pqxx>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace firefly {

ConnectionPool::ConnectionPool(std::string url, int max_size,
                               absl::Duration acquire_timeout)
    : url_(std::move(url)),
      max_size_(max_size),
      acquire_timeout_(acquire_timeout) {}

absl::StatusOr<std::unique_ptr<pqxx::connection>> ConnectionPool::Acquire() {
  const auto wait_started = std::chrono::steady_clock::now();
  {
    std::unique_lock<std::mutex> lock(mu_);
    const bool available = cv_.wait_for(
        lock, absl::ToChronoMilliseconds(acquire_timeout_),
        [this] { return !idle_.empty() || open_count_ < max_size_; });
    if (!available) {
      const int idle_count = static_cast<int>(idle_.size());
      LOG(WARNING) << "database pool acquisition timed out after "
                   << absl::FormatDuration(acquire_timeout_) << " (open "
                   << open_count_ << "/" << max_size_ << ", idle "
                   << idle_count << ")";
      return absl::DeadlineExceededError("database connection pool exhausted");
    }
    const auto waited = std::chrono::steady_clock::now() - wait_started;
    if (waited >= std::chrono::milliseconds(1)) {
      VLOG(1) << "database pool acquisition waited "
              << std::chrono::duration_cast<std::chrono::milliseconds>(waited)
                     .count()
              << "ms (open " << open_count_ << "/" << max_size_ << ", idle "
              << idle_.size() << ")";
    }
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
