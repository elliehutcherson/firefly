include(GoogleTest)

add_executable(greeting_test greeting_test.cc)
target_include_directories(greeting_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(greeting_test PRIVATE firefly_lib GTest::gtest_main)
gtest_discover_tests(greeting_test)
