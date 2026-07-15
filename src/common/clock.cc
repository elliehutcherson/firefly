#include "src/common/clock.h"

#include <memory>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace firefly {
namespace {

class SystemClock : public Clock {
 public:
  absl::Time Now() const override { return absl::Now(); }
  void Sleep(absl::Duration duration) const override {
    absl::SleepFor(duration);
  }
};

}  // namespace

std::unique_ptr<Clock> CreateSystemClock() {
  return std::make_unique<SystemClock>();
}

}  // namespace firefly
