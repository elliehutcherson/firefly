# Prefer clang so the build and clang-tidy/clangd agree on available language
# features (CMAKE_SYSTEM_NAME is not set yet this early, so probe the PATH).
# Falls back to the default compiler where clang is absent (e.g. Docker).
if(NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(FIREFLY_CLANG clang)
  find_program(FIREFLY_CLANGXX clang++)
  if(FIREFLY_CLANG AND FIREFLY_CLANGXX)
    set(CMAKE_C_COMPILER ${FIREFLY_CLANG})
    set(CMAKE_CXX_COMPILER ${FIREFLY_CLANGXX})
  endif()
endif()

cmake_minimum_required(VERSION 3.16)
project(firefly CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ## System C libraries (stable ABI, not worth building in-tree).
# Everything else is a git submodule under include/ — see docs/ARCHITECTURE.md.
find_package(PostgreSQL QUIET)
find_package(CURL QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SODIUM QUIET libsodium)
endif()
if(NOT PostgreSQL_FOUND OR NOT CURL_FOUND OR NOT SODIUM_FOUND)
  message(FATAL_ERROR
      "Missing system libraries. Install them with:\n"
      "  sudo apt install pkg-config libpq-dev libcurl4-openssl-dev libsodium-dev")
endif()

# googletest must precede abseil: abseil's test helpers (absl::status_matchers
# etc.) link against the external GTest targets.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(include/googletest)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
set(ABSL_FIND_GOOGLETEST OFF)
set(ABSL_BUILD_TEST_HELPERS ON)
add_subdirectory(include/abseil-cpp)

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(include/nlohmann)

set(SKIP_BUILD_TEST ON CACHE BOOL "" FORCE)
add_subdirectory(include/libpqxx)

set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
add_subdirectory(include/cpr)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
include(${PROJECT_SOURCE_DIR}/cmake/crow.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/sodium.cmake)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
