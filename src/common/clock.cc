#include "src/common/clock.h"

#include <memory>

#include "absl/log/log.h"
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

absl::TimeZone NewYorkTimeZone() {
  absl::TimeZone tz;
  if (!absl::LoadTimeZone("America/New_York", &tz)) {
    // tzdata missing would be a broken image; EST keeps us roughly right.
    LOG(ERROR) << "America/New_York tzdata unavailable; using fixed EST";
    return absl::FixedTimeZone(-5 * 60 * 60);
  }
  return tz;
}

}  // namespace firefly
