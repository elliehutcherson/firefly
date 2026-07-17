#include "marketdata/instrument_repo.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"
#include "db/db.h"
#include "db/row_reader.h"

namespace firefly {

absl::StatusOr<std::vector<Symbol>> InstrumentRepo::ListActiveSymbols() {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT symbol FROM instruments WHERE is_active "
                 "ORDER BY symbol"));
  std::vector<Symbol> symbols;
  symbols.reserve(rows.size());
  for (const Row& row : rows) {
    ASSIGN_OR_RETURN(
        const absl::string_view raw,
        RowReader(row, "instruments").RequiredString(0));
    // The 0003 regex CHECK guarantees stored symbols parse.
    ASSIGN_OR_RETURN(const Symbol symbol, Symbol::Parse(raw));
    symbols.push_back(symbol);
  }
  return symbols;
}

absl::StatusOr<bool> InstrumentRepo::Exists(const Symbol& symbol) {
  ASSIGN_OR_RETURN(
      const Rows rows,
      db_.Query("SELECT 1 FROM instruments WHERE symbol = $1 AND is_active",
                 {symbol.str()}));
  return !rows.empty();
}

}  // namespace firefly
