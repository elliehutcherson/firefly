#ifndef FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_
#define FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_

#include <vector>

#include "absl/status/statusor.h"
#include "common/symbol.h"

namespace firefly {

// Market data's persistence boundary for the curated symbol universe.
class InstrumentStore {
 public:
  virtual ~InstrumentStore() = default;

  virtual absl::StatusOr<std::vector<Symbol>> ListActiveSymbols() = 0;
  virtual absl::StatusOr<bool> Exists(const Symbol& symbol) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_
