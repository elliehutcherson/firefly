#ifndef FIREFLY_MARKETDATA_CACHED_INSTRUMENT_STORE_H_
#define FIREFLY_MARKETDATA_CACHED_INSTRUMENT_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/marketdata/instrument_store.h"

namespace firefly {

// Atomic snapshot of the active instrument universe. Refresh builds a
// complete immutable replacement before publishing it, so readers
// see either the old or new universe and a failed refresh keeps known-good
// data. Refresh() must succeed once before the store is served.
class CachedInstrumentStore : public InstrumentStore {
 public:
  // `source` is borrowed and must outlive this store.
  explicit CachedInstrumentStore(InstrumentStore& source) : source_(source) {}

  absl::StatusOr<std::vector<std::string>> ListActiveSymbols() override;
  absl::StatusOr<bool> Exists(const std::string& symbol) override;

  // Reloads the full active universe from the source of truth. Safe to call
  // while readers use the current snapshot.
  absl::Status Refresh();

 private:
  struct Snapshot {
    std::vector<std::string> symbols;
    absl::flat_hash_set<std::string> membership;
  };

  InstrumentStore& source_;
  // Access only through std::atomic_load/store. The free functions support
  // atomic shared_ptr publication on libc++ versions predating C++20's
  // atomic<shared_ptr> specialization.
  std::shared_ptr<const Snapshot> snapshot_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_CACHED_INSTRUMENT_STORE_H_
