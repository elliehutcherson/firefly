#include "src/api/rate_limiter.h"

#include <algorithm>
#include <string>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace firefly {

bool TokenBucketRateLimiter::Allow(const std::string& key) {
  const absl::Time now = clock_.Now();
  absl::MutexLock lock(&mu_);

  auto it = buckets_.find(key);
  if (it == buckets_.end()) {
    if (buckets_.size() >= options_.max_keys) {
      // Sweep buckets that have refilled to capacity — idle keys.
      for (auto sweep = buckets_.begin(); sweep != buckets_.end();) {
        const double refilled =
            sweep->second.tokens +
            absl::ToDoubleSeconds(now - sweep->second.last_refill) *
                options_.tokens_per_second;
        if (refilled >= options_.burst) {
          buckets_.erase(sweep++);
        } else {
          ++sweep;
        }
      }
      if (buckets_.size() >= options_.max_keys) {
        LOG(WARNING) << "rate limiter at " << buckets_.size()
                     << " active keys; failing open";
        return true;
      }
    }
    it = buckets_.try_emplace(key, Bucket{static_cast<double>(options_.burst),
                                          now})
             .first;
  }

  Bucket& bucket = it->second;
  bucket.tokens = std::min(
      static_cast<double>(options_.burst),
      bucket.tokens + absl::ToDoubleSeconds(now - bucket.last_refill) *
                          options_.tokens_per_second);
  bucket.last_refill = now;
  if (bucket.tokens < 1.0) {
    return false;
  }
  bucket.tokens -= 1.0;
  return true;
}

}  // namespace firefly
