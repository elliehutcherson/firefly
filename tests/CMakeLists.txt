include(GoogleTest)

add_executable(config_test config_test.cc)
target_include_directories(config_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(config_test PRIVATE firefly_lib GTest::gtest_main)
gtest_discover_tests(config_test)

add_executable(db_test db_test.cc)
target_include_directories(db_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(db_test PRIVATE
    firefly_lib
    absl::status_matchers
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(db_test)

add_executable(money_test money_test.cc)
target_include_directories(money_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(money_test PRIVATE firefly_lib GTest::gtest_main)
gtest_discover_tests(money_test)

add_executable(alpaca_test alpaca_test.cc)
target_include_directories(alpaca_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(alpaca_test PRIVATE
    firefly_lib
    absl::status_matchers
    GTest::gmock
    GTest::gtest_main)
gtest_discover_tests(alpaca_test)
