#ifndef FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_
#define FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace firefly {

// Market data's persistence boundary for the curated symbol universe.
class InstrumentStore {
 public:
  virtual ~InstrumentStore() = default;

  virtual absl::StatusOr<std::vector<std::string>> ListActiveSymbols() = 0;
  virtual absl::StatusOr<bool> Exists(const std::string& symbol) = 0;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_INSTRUMENT_STORE_H_
