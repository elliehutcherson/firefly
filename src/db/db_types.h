#ifndef FIREFLY_DB_DB_TYPES_H_
#define FIREFLY_DB_DB_TYPES_H_

#include <optional>
#include <string>
#include <vector>

namespace firefly {

// One query result row. Values arrive as text; NULL columns are nullopt.
struct Row {
  std::vector<std::optional<std::string>> columns;
};

using Rows = std::vector<Row>;

// Positional statement parameters ($1, $2, ...); nullopt binds SQL NULL.
using DbParams = std::vector<std::optional<std::string>>;

}  // namespace firefly

#endif  // FIREFLY_DB_DB_TYPES_H_
