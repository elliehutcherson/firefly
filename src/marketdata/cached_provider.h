#ifndef FIREFLY_MARKETDATA_CACHED_PROVIDER_H_
#define FIREFLY_MARKETDATA_CACHED_PROVIDER_H_

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
// No eviction thread: callers validate symbols against the instruments
// universe *before* calling, so the key space is bounded (~800 symbols) and
// stale entries are simply replaced on their next lookup. Minute-bar keys
// include the requested range; callers quantize ranges to whole minutes so
// requests within the same minute share one key.
//
// Thread-safe. The mutex only guards map lookups — never the upstream call.
class CachedProvider : public MarketDataProvider {
 public:
  // `inner` and `clock` are borrowed and must outlive the provider.
  CachedProvider(MarketDataProvider* inner, const Clock* clock,
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
                               absl::Duration ttl,
                               absl::FunctionRef<absl::StatusOr<V>()> fetch);

  MarketDataProvider* const inner_;
  const Clock* const clock_;
  const CacheOptions options_;

  Cache<Trade> trades_;
  Cache<std::vector<Bar>> minute_bars_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CACHED_PROVIDER_H_
