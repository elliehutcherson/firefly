#ifndef FIREFLY_MARKETDATA_CACHED_PROVIDER_H_
#define FIREFLY_MARKETDATA_CACHED_PROVIDER_H_

#include <cstddef>
#include <future>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/common/clock.h"
#include "src/marketdata/provider.h"

namespace firefly {

struct CacheOptions {
  // Trades execute at this price, so keep it fresh (docs/ARCHITECTURE.md,
  // "Anti-cheat pricing rule"); the Cloudflare edge adds its own ~60s.
  absl::Duration quote_ttl = absl::Seconds(30);
  absl::Duration minute_bars_ttl = absl::Seconds(90);
  // Hard bounds for completed entries. Concurrent in-flight fetches may
  // temporarily exceed these limits; they are never evicted because waiters
  // must retain request coalescing.
  size_t max_quote_entries = 1024;
  size_t max_minute_bar_entries = 4096;
};

// Caching + request-coalescing decorator over a MarketDataProvider
// (docs/ARCHITECTURE.md "Caching layers" 2 and 3). Everything downstream —
// api/ handlers, trade execution, the movers job — takes the same narrow
// MarketDataProvider interface and gets both layers transparently.
//
//   * Latest trades and minute bars are cached per key with a TTL.
//   * Concurrent misses for one key share a single upstream call: the first
//     caller fetches, the rest block on the same shared_future (blocking a
//     Crow worker is the documented threading model).
//   * Errors are delivered to the callers waiting on them but never cached;
//     the next request retries upstream.
//   * GetDailyBars passes through uncached: daily charts are served from
//     Postgres and never reach a provider at request time.
//
// No eviction thread: insertion and fetch completion opportunistically remove
// expired entries, then evict completed entries with the earliest expiration
// until the configured bound is met. In-flight entries are never evicted.
//
// Thread-safe. The mutex only guards map lookups — never the upstream call.
class CachedProvider : public MarketDataProvider {
 public:
  // `inner` and `clock` are borrowed and must outlive the provider.
  CachedProvider(MarketDataProvider& inner, const Clock& clock,
                 CacheOptions options)
      : inner_(inner), clock_(clock), options_(options) {}

  absl::StatusOr<Trade> GetLatestTrade(const std::string& symbol) override;
  absl::StatusOr<std::vector<Bar>> GetDailyBars(const std::string& symbol,
                                                absl::CivilDay start,
                                                absl::CivilDay end) override;
  absl::StatusOr<std::vector<Bar>> GetMinuteBars(const std::string& symbol,
                                                 absl::Time start,
                                                 absl::Time end) override;

 private:
  template <typename V>
  struct Cache {
    struct Entry {
      std::shared_future<absl::StatusOr<V>> future;
      // InfiniteFuture() while the fetch is in flight, so concurrent misses
      // wait on the future instead of fetching again.
      absl::Time expires_at;
    };
    absl::Mutex mu;
    absl::flat_hash_map<std::string, Entry> entries ABSL_GUARDED_BY(mu);
  };

  // Returns the cached value for `key`, joining an in-flight fetch when one
  // exists, or calls `fetch` (outside the lock) and caches the result.
  template <typename V>
  absl::StatusOr<V> GetOrFetch(Cache<V>& cache, const std::string& key,
                               absl::Duration ttl, size_t max_entries,
                               absl::FunctionRef<absl::StatusOr<V>()> fetch);

  // Removes expired completed entries, then the completed entries nearest
  // expiration, until `cache` is at or below `target_size`. Must be called
  // while holding cache.mu. In-flight entries may keep it temporarily larger.
  template <typename V>
  void Trim(Cache<V>& cache, absl::Time now, size_t target_size)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(cache.mu);

  MarketDataProvider& inner_;
  const Clock& clock_;
  const CacheOptions options_;

  Cache<Trade> trades_;
  Cache<std::vector<Bar>> minute_bars_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CACHED_PROVIDER_H_
