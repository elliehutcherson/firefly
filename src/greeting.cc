#include "src/greeting.h"

#include "absl/strings/str_format.h"

namespace firefly {

std::string MakeGreeting(const std::string& name) {
  return absl::StrFormat("Hello, %s!", name);
}

}  // namespace firefly
