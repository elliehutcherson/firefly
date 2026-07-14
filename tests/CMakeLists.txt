include(GoogleTest)

# Shared dependencies for every test target: the project lib, status
# matchers, gmock/gtest, and the repo root on the include path (for
# tests/status_matchers.h and tests/fakes/).
add_library(firefly_test_support INTERFACE)
target_include_directories(firefly_test_support INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(firefly_test_support INTERFACE
    firefly_lib
    absl::status_matchers
    GTest::gmock
    GTest::gtest_main)

add_executable(config_test config_test.cc)
target_link_libraries(config_test PRIVATE firefly_test_support)
gtest_discover_tests(config_test)

# Integration tests against real Postgres (self-skip when unreachable);
# labeled e2e so the default suite stays hermetic. Run with: ctest -L e2e
add_executable(db_test db_test.cc)
target_link_libraries(db_test PRIVATE firefly_test_support)
gtest_discover_tests(db_test PROPERTIES LABELS e2e)

add_executable(money_test money_test.cc)
target_link_libraries(money_test PRIVATE firefly_test_support)
gtest_discover_tests(money_test)

add_executable(alpaca_test alpaca_test.cc)
target_link_libraries(alpaca_test PRIVATE firefly_test_support)
gtest_discover_tests(alpaca_test)

add_executable(http_test http_test.cc)
target_link_libraries(http_test PRIVATE firefly_test_support)
gtest_discover_tests(http_test)

add_executable(server_test server_test.cc)
target_link_libraries(server_test PRIVATE firefly_test_support)
gtest_discover_tests(server_test)

# Hits the real Alpaca API; self-skips without APCA_API_KEY_ID /
# APCA_API_SECRET_KEY in the environment.
add_executable(alpaca_e2e_test e2e/alpaca_e2e_test.cc)
target_link_libraries(alpaca_e2e_test PRIVATE firefly_test_support)
gtest_discover_tests(alpaca_e2e_test PROPERTIES LABELS e2e)
