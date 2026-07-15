#include "src/marketdata/instrument_repo.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/common/db.h"
#include "src/common/status_macros.h"

namespace firefly {

absl::StatusOr<std::vector<std::string>> InstrumentRepo::ListActiveSymbols() {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT symbol FROM instruments WHERE is_active "
                 "ORDER BY symbol"));
  std::vector<std::string> symbols;
  symbols.reserve(rows.size());
  for (const Row& row : rows) {
    if (row.columns.empty() || !row.columns[0].has_value()) {
      return absl::InternalError("instruments row with NULL symbol");
    }
    symbols.push_back(*row.columns[0]);
  }
  return symbols;
}

absl::StatusOr<bool> InstrumentRepo::Exists(const std::string& symbol) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_->Query("SELECT 1 FROM instruments WHERE symbol = $1 AND is_active",
                 {symbol}));
  return !rows.empty();
}

}  // namespace firefly
