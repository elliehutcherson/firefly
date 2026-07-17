#ifndef FIREFLY_JOBS_JOB_RUNNER_H_
#define FIREFLY_JOBS_JOB_RUNNER_H_

#include <functional>
#include <string>
#include <thread>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace firefly {

// Runs `tick` on its own thread: once immediately at Start(), then every
// `period` until Stop() (or destruction). The in-process background timer
// from docs/ARCHITECTURE.md; one instance per job (bar sync, movers, ...).
//
// `tick` must not throw. A tick that overruns the period delays the next
// tick; ticks never overlap.
class JobRunner {
 public:
  JobRunner(std::string name, absl::Duration period,
            std::function<void()> tick)
      : name_(std::move(name)), period_(period), tick_(std::move(tick)) {}

  // Stops and joins.
  ~JobRunner() { Stop(); }

  JobRunner(const JobRunner&) = delete;
  JobRunner& operator=(const JobRunner&) = delete;

  // Starts the thread. Call at most once.
  void Start();

  // Requests stop and joins the thread. Prompt even mid-period; safe to call
  // repeatedly or without Start().
  void Stop();

 private:
  const std::string name_;
  const absl::Duration period_;
  const std::function<void()> tick_;

  absl::Mutex mu_;
  bool stop_requested_ ABSL_GUARDED_BY(mu_) = false;
  std::thread thread_;
};

}  // namespace firefly

#endif  // FIREFLY_JOBS_JOB_RUNNER_H_
