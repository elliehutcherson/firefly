# Crow (header-only) + standalone asio, exposed as a single interface target.
# We skip Crow's own CMake so it doesn't go looking for a system asio; both
# libraries are just include paths.
find_package(Threads REQUIRED)

add_library(crow INTERFACE)
target_include_directories(crow SYSTEM INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/crow/include
    ${CMAKE_SOURCE_DIR}/third_party/asio/asio/include)
target_link_libraries(crow INTERFACE Threads::Threads)
