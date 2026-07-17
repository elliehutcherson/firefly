#include "common/symbol.h"

#include <sstream>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "support/status_matchers.h"

namespace firefly {
namespace {

using ::absl_testing::StatusIs;

TEST(SymbolTest, ParsesAndUppercases) {
  const absl::StatusOr<Symbol> lower = Symbol::Parse("aapl");
  ASSERT_OK(lower);
  EXPECT_EQ(lower->str(), "AAPL");
  const absl::StatusOr<Symbol> dotted = Symbol::Parse("brk.b");
  ASSERT_OK(dotted);
  EXPECT_EQ(dotted->str(), "BRK.B");
  ASSERT_OK(Symbol::Parse("A"));
  const absl::StatusOr<Symbol> max = Symbol::Parse("ABCDEFGHIJ");
  ASSERT_OK(max);
  EXPECT_EQ(max->str().size(), Symbol::kMaxLength);
}

TEST(SymbolTest, RejectsInjectionAndJunk) {
  for (const char* junk :
       {"", ".", "-", ".AAPL", "-AAPL", "AAPL; DROP TABLE users",
        "ABCDEFGHIJK", "AA PL", "AAPL'", "aapl%27", "A/B", "1AAPL", "..",
        "AAPL/../..", "тест"}) {
    EXPECT_THAT(Symbol::Parse(junk),
                StatusIs(absl::StatusCode::kInvalidArgument))
        << "input: '" << junk << "'";
  }
}

TEST(SymbolTest, ComparesAndOrders) {
  const Symbol aapl = *Symbol::Parse("AAPL");
  const Symbol msft = *Symbol::Parse("MSFT");
  EXPECT_EQ(aapl, *Symbol::Parse("aapl"));
  EXPECT_NE(aapl, msft);
  EXPECT_LT(aapl, msft);
}

TEST(SymbolTest, HashesInAbslContainers) {
  absl::flat_hash_set<Symbol> universe;
  universe.insert(*Symbol::Parse("AAPL"));
  universe.insert(*Symbol::Parse("aapl"));  // Same symbol.
  universe.insert(*Symbol::Parse("MSFT"));
  EXPECT_EQ(universe.size(), 2);
  EXPECT_TRUE(universe.contains(*Symbol::Parse("AAPL")));
}

TEST(SymbolTest, ConvertsOutImplicitly) {
  const Symbol symbol = *Symbol::Parse("AAPL");
  const absl::string_view view = symbol;
  EXPECT_EQ(view, "AAPL");
  EXPECT_EQ(absl::StrCat("/v2/stocks/", symbol.str(), "/bars"),
            "/v2/stocks/AAPL/bars");
  std::ostringstream os;
  os << symbol;
  EXPECT_EQ(os.str(), "AAPL");
}

}  // namespace
}  // namespace firefly
