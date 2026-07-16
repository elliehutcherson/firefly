#ifndef FIREFLY_API_RATE_LIMITER_H_
#define FIREFLY_API_RATE_LIMITER_H_

#include <cstddef>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "src/common/clock.h"

namespace firefly {

// Token buckets over opaque string keys ("ip:1.2.3.4" now, "session:..." when
// trading lands). One bucket per key: `burst` capacity, refilled continuously
// at `tokens_per_second`, computed lazily from the injected Clock.
//
// Memory bounding: at `max_keys` the map is swept of full (idle) buckets; if
// every bucket is active the limiter FAILS OPEN with a warning — an attacker
// who can rotate that many source IPs defeats per-IP limiting anyway, and
// availability for real users wins. Thread-safe.
class TokenBucketRateLimiter {
 public:
  struct Options {
    double tokens_per_second = 0.2;
    int burst = 10;
    size_t max_keys = 10000;
  };

  // `clock` is borrowed and must outlive the limiter.
  TokenBucketRateLimiter(const Clock* clock, Options options)
      : clock_(clock), options_(options) {}

  // Consumes one token from `key`'s bucket; false = rate limited.
  bool Allow(const std::string& key);

 private:
  struct Bucket {
    double tokens = 0;
    absl::Time last_refill;
  };

  const Clock* const clock_;
  const Options options_;

  absl::Mutex mu_;
  absl::flat_hash_map<std::string, Bucket> buckets_ ABSL_GUARDED_BY(mu_);
};

}  // namespace firefly

#endif  // FIREFLY_API_RATE_LIMITER_H_
