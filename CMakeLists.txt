cmake_minimum_required(VERSION 3.16)
project(firefly CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(include/abseil-cpp)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(include/googletest)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
