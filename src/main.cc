#include <iostream>

#include "src/greeting.h"

int main() {
  std::cout << firefly::MakeGreeting("world") << std::endl;
  return 0;
}
