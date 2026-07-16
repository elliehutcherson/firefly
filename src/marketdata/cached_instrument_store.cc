#include "src/marketdata/cached_instrument_store.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "src/common/status_macros.h"

namespace firefly {

absl::StatusOr<std::vector<std::string>>
CachedInstrumentStore::ListActiveSymbols() {
  const std::shared_ptr<const Snapshot> snapshot =
      std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  if (snapshot == nullptr) {
    return absl::UnavailableError("instrument universe is not loaded");
  }
  return snapshot->symbols;
}

absl::StatusOr<bool> CachedInstrumentStore::Exists(const std::string& symbol) {
  const std::shared_ptr<const Snapshot> snapshot =
      std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  if (snapshot == nullptr) {
    return absl::UnavailableError("instrument universe is not loaded");
  }
  return snapshot->membership.contains(symbol);
}

absl::Status CachedInstrumentStore::Refresh() {
  ASSIGN_OR_RETURN(std::vector<std::string> symbols,
                   source_.ListActiveSymbols());
  if (symbols.empty()) {
    return absl::FailedPreconditionError("active instrument universe is empty");
  }
  std::sort(symbols.begin(), symbols.end());
  symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());

  auto next = std::make_shared<Snapshot>();
  next->symbols = std::move(symbols);
  next->membership.reserve(next->symbols.size());
  next->membership.insert(next->symbols.begin(), next->symbols.end());
  std::shared_ptr<const Snapshot> published = std::move(next);
  std::atomic_store_explicit(&snapshot_, std::move(published),
                             std::memory_order_release);
  return absl::OkStatus();
}

}  // namespace firefly
