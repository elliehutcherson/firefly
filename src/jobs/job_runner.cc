#include "src/jobs/job_runner.h"

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace firefly {

void JobRunner::Start() {
  LOG(INFO) << "job " << name_ << " starting, period " << period_;
  thread_ = std::thread([this] {
    while (true) {
      tick_();
      absl::MutexLock lock(&mu_);
      // Wakes immediately when Stop() flips the flag.
      if (mu_.AwaitWithTimeout(absl::Condition(&stop_requested_), period_)) {
        return;
      }
    }
  });
}

void JobRunner::Stop() {
  {
    absl::MutexLock lock(&mu_);
    stop_requested_ = true;
  }
  if (thread_.joinable()) {
    thread_.join();
    LOG(INFO) << "job " << name_ << " stopped";
  }
}

}  // namespace firefly
