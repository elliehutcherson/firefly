#include "src/greeting.h"

#include <gtest/gtest.h>

namespace firefly {
namespace {

TEST(GreetingTest, MakeGreetingIncludesName) {
  EXPECT_EQ(MakeGreeting("Firefly"), "Hello, Firefly!");
}

}  // namespace
}  // namespace firefly
