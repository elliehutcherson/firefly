#ifndef FIREFLY_COMMON_CLOCK_H_
#define FIREFLY_COMMON_CLOCK_H_

#include <memory>

#include "absl/time/time.h"

namespace firefly {

// Wall clock behind a project-owned interface so time-dependent code (TTL
// caches, background jobs, chart range math) is testable with a fake.
// Production code injects CreateSystemClock(); absl::Now() and
// absl::SleepFor() are never called above this wrapper.
//
// Methods are const so consumers can hold a `const Clock*`; fakes keep their
// state mutable.
class Clock {
 public:
  virtual ~Clock() = default;

  virtual absl::Time Now() const = 0;

  // Blocks the calling thread; used to throttle upstream API calls.
  virtual void Sleep(absl::Duration duration) const = 0;
};

// Backed by absl::Now / absl::SleepFor.
std::unique_ptr<Clock> CreateSystemClock();

}  // namespace firefly

#endif  // FIREFLY_COMMON_CLOCK_H_
