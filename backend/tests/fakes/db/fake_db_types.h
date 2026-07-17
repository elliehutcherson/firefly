#ifndef FIREFLY_TESTS_FAKES_FAKE_DB_TYPES_H_
#define FIREFLY_TESTS_FAKES_FAKE_DB_TYPES_H_

#include <string>

#include "db/db_types.h"

namespace firefly {

// One statement recorded by FakeDb or FakeTransaction.
struct FakeDbCall {
  std::string sql;
  DbParams params;
};

}  // namespace firefly

#endif  // FIREFLY_TESTS_FAKES_FAKE_DB_TYPES_H_
