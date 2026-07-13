add_library(firefly_lib
    api/server.cc
    common/config.cc
    common/db.cc
    common/db_pool.cc
    common/http.cc
    common/money.cc
    marketdata/alpaca.cc)
target_include_directories(firefly_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(firefly_lib PUBLIC
    absl::log
    absl::log_initialize
    absl::status
    absl::statusor
    absl::str_format
    absl::strings
    absl::time
    crow
    nlohmann_json::nlohmann_json
    pqxx)
target_link_libraries(firefly_lib PRIVATE cpr::cpr)

add_executable(firefly main.cc)
target_link_libraries(firefly PRIVATE firefly_lib)

add_executable(alpaca_probe tools/alpaca_probe.cc)
target_link_libraries(alpaca_probe PRIVATE firefly_lib)
