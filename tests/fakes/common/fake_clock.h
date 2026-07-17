#ifndef FIREFLY_TESTS_FAKES_FAKE_CLOCK_H_
#define FIREFLY_TESTS_FAKES_FAKE_CLOCK_H_

#include <vector>

#include "absl/time/time.h"
#include "common/clock.h"

namespace firefly {

// Manually advanced clock. Sleep() advances time instead of blocking and
// records each requested duration. Members are mutable because the Clock
// interface is const (see clock.h).
class FakeClock : public Clock {
 public:
  explicit FakeClock(absl::Time now) : now_(now) {}

  absl::Time Now() const override { return now_; }

  void Sleep(absl::Duration duration) const override {
    now_ += duration;
    sleeps_.push_back(duration);
  }

  void SetNow(absl::Time now) { now_ = now; }
  void Advance(absl::Duration duration) { now_ += duration; }
  const std::vector<absl::Duration>& sleeps() const { return sleeps_; }

 private:
  mutable absl::Time now_;
  mutable std::vector<absl::Duration> sleeps_;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_CLOCK_H_
