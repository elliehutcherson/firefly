add_library(firefly_lib
    api/market_handlers.cc
    api/server.cc
    common/clock.cc
    common/config.cc
    common/db.cc
    common/db_pool.cc
    common/http.cc
    common/money.cc
    jobs/daily_bar_sync.cc
    jobs/job_runner.cc
    marketdata/alpaca.cc
    marketdata/cached_provider.cc
    marketdata/candle_repo.cc
    marketdata/instrument_repo.cc)
target_include_directories(firefly_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(firefly_lib PUBLIC
    absl::flat_hash_map
    absl::log
    absl::log_initialize
    absl::status
    absl::statusor
    absl::str_format
    absl::strings
    absl::synchronization
    absl::time
    crow
    nlohmann_json::nlohmann_json
    pqxx)
target_link_libraries(firefly_lib PRIVATE cpr::cpr)

add_executable(firefly main.cc)
target_link_libraries(firefly PRIVATE firefly_lib)

add_executable(alpaca_probe tools/alpaca_probe.cc)
target_link_libraries(alpaca_probe PRIVATE firefly_lib)

add_executable(backfill_bars tools/backfill_bars.cc)
target_link_libraries(backfill_bars PRIVATE
    firefly_lib
    absl::flags
    absl::flags_parse
    absl::flags_usage)
