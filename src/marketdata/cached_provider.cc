#include "src/marketdata/cached_provider.h"

#include <cstddef>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "src/marketdata/provider.h"

namespace firefly {

template <typename V>
void CachedProvider::Trim(Cache<V>& cache, absl::Time now,
                          size_t target_size) {
  for (auto it = cache.entries.begin(); it != cache.entries.end();) {
    if (it->second.expires_at != absl::InfiniteFuture() &&
        it->second.expires_at <= now) {
      cache.entries.erase(it++);
    } else {
      ++it;
    }
  }
  while (cache.entries.size() > target_size) {
    auto victim = cache.entries.end();
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
      if (it->second.expires_at == absl::InfiniteFuture()) {
        continue;
      }
      if (victim == cache.entries.end() ||
          it->second.expires_at < victim->second.expires_at) {
        victim = it;
      }
    }
    if (victim == cache.entries.end()) {
      return;  // Every entry is in flight; allow temporary overflow.
    }
    cache.entries.erase(victim);
  }
}

template <typename V>
absl::StatusOr<V> CachedProvider::GetOrFetch(
    Cache<V>& cache, const std::string& key, absl::Duration ttl,
    size_t max_entries,
    absl::FunctionRef<absl::StatusOr<V>()> fetch) {
  std::promise<absl::StatusOr<V>> promise;
  std::shared_future<absl::StatusOr<V>> future;
  bool fetching = false;
  {
    absl::MutexLock lock(&cache.mu);
    const auto it = cache.entries.find(key);
    if (it != cache.entries.end() && clock_.Now() < it->second.expires_at) {
      future = it->second.future;  // Fresh, or in flight: share it.
    } else {
      fetching = true;
      future = promise.get_future().share();
      // Reserve room when possible. An expired entry for this key is included
      // in the sweep; in-flight entries may temporarily exceed the target.
      Trim(cache, clock_.Now(), max_entries > 0 ? max_entries - 1 : 0);
      cache.entries[key] = {future, absl::InfiniteFuture()};
    }
  }
  if (!fetching) {
    return future.get();
  }

  absl::StatusOr<V> result = fetch();  // Never under the lock.
  promise.set_value(result);
  {
    absl::MutexLock lock(&cache.mu);
    // The entry is still ours: an in-flight entry is never replaced.
    if (result.ok()) {
      cache.entries[key].expires_at = clock_.Now() + ttl;
      Trim(cache, clock_.Now(), max_entries);
    } else {
      // Waiters already hold the future; nobody after them sees the error.
      cache.entries.erase(key);
    }
  }
  return result;
}

absl::StatusOr<Trade> CachedProvider::GetLatestTrade(
    const Symbol& symbol) {
  return GetOrFetch<Trade>(trades_, symbol.str(), options_.quote_ttl,
                           options_.max_quote_entries,
                           [&] { return inner_.GetLatestTrade(symbol); });
}

absl::StatusOr<std::vector<Bar>> CachedProvider::GetDailyBars(
    const Symbol& symbol, absl::CivilDay start, absl::CivilDay end) {
  // Uncached by design: daily history lives in candles_daily, and the only
  // callers of this method are backfill/sync jobs.
  return inner_.GetDailyBars(symbol, start, end);
}

absl::StatusOr<std::vector<Bar>> CachedProvider::GetMinuteBars(
    const Symbol& symbol, absl::Time start, absl::Time end) {
  const std::string key = absl::StrCat(
      symbol.str(), "|",
      absl::FormatTime(absl::RFC3339_full, start, absl::UTCTimeZone()), "|",
      absl::FormatTime(absl::RFC3339_full, end, absl::UTCTimeZone()));
  return GetOrFetch<std::vector<Bar>>(
      minute_bars_, key, options_.minute_bars_ttl,
      options_.max_minute_bar_entries,
      [&] { return inner_.GetMinuteBars(symbol, start, end); });
}

}  // namespace firefly
