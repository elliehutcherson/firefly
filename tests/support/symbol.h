#ifndef FIREFLY_TESTS_SUPPORT_SYMBOL_H_
#define FIREFLY_TESTS_SUPPORT_SYMBOL_H_

#include "absl/strings/string_view.h"
#include "src/common/symbol.h"

namespace firefly {

// Parse-or-die for test literals; validity is Symbol's own tested contract.
inline Symbol Sym(absl::string_view raw) { return *Symbol::Parse(raw); }

}  // namespace firefly

#endif  // FIREFLY_TESTS_SUPPORT_SYMBOL_H_
