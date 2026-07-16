#include "src/jobs/job_runner.h"

#include <atomic>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace firefly {
namespace {

TEST(JobRunnerTest, TicksImmediatelyOnStart) {
  std::atomic<int> ticks{0};
  absl::Notification first_tick;
  JobRunner runner("test", absl::Minutes(10), [&] {
    if (ticks.fetch_add(1) == 0) {
      first_tick.Notify();
    }
  });

  runner.Start();
  // Generous deadline; the period is long so exactly one tick fires.
  ASSERT_TRUE(first_tick.WaitForNotificationWithTimeout(absl::Seconds(5)));
  runner.Stop();
  EXPECT_EQ(ticks.load(), 1);
}

TEST(JobRunnerTest, StopIsPromptMidPeriod) {
  absl::Notification first_tick;
  JobRunner runner("test", absl::Minutes(10), [&] {
    if (!first_tick.HasBeenNotified()) {
      first_tick.Notify();
    }
  });

  runner.Start();
  ASSERT_TRUE(first_tick.WaitForNotificationWithTimeout(absl::Seconds(5)));
  const absl::Time before = absl::Now();
  runner.Stop();  // Mid-period: must not wait out the 10 minutes.
  EXPECT_LT(absl::Now() - before, absl::Seconds(5));
}

TEST(JobRunnerTest, StopWithoutStartIsSafe) {
  JobRunner runner("test", absl::Minutes(10), [] {});
  runner.Stop();
}

TEST(JobRunnerTest, DestructorStops) {
  absl::Notification first_tick;
  {
    JobRunner runner("test", absl::Minutes(10), [&] {
      if (!first_tick.HasBeenNotified()) {
        first_tick.Notify();
      }
    });
    runner.Start();
    ASSERT_TRUE(first_tick.WaitForNotificationWithTimeout(absl::Seconds(5)));
  }  // Destructor joins; reaching the next line is the assertion.
}

}  // namespace
}  // namespace firefly
