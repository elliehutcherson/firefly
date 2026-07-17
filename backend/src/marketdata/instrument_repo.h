#ifndef FIREFLY_MARKETDATA_INSTRUMENT_REPO_H_
#define FIREFLY_MARKETDATA_INSTRUMENT_REPO_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "db/db.h"
#include "marketdata/instrument_store.h"
#include "common/symbol.h"

namespace firefly {

// Reads over the curated symbol universe (instruments table, seeded by
// migrations). Inactive symbols — removed by index rebalances — are invisible
// here; their historical rows stay in the database.
class InstrumentRepo : public InstrumentStore {
 public:
  // `db` is borrowed and must outlive the repo.
  explicit InstrumentRepo(Db& db) : db_(db) {}

  // All active symbols, sorted. The universe the sync job iterates.
  absl::StatusOr<std::vector<Symbol>> ListActiveSymbols() override;

  // Whether `symbol` (exact, uppercase) is active. The api/ boundary check:
  // requests for symbols outside the universe never reach a provider.
  absl::StatusOr<bool> Exists(const Symbol& symbol) override;

 private:
  Db& db_;
};

}  // namespace firefly

#endif  // FIREFLY_MARKETDATA_INSTRUMENT_REPO_H_
