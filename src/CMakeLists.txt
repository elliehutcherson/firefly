add_library(firefly_lib
    api/auth_handlers.cc
    api/market_handlers.cc
    api/rate_limiter.cc
    api/server.cc
    api/status_mapping.cc
    api/trading_handlers.cc
    auth/crypto.cc
    auth/session_auth.cc
    auth/session_repo.cc
    auth/turnstile.cc
    auth/user_repo.cc
    common/clock.cc
    common/config.cc
    common/http.cc
    common/money.cc
    common/symbol.cc
    db/connection_pool.cc
    db/db.cc
    db/row_reader.cc
    jobs/daily_bar_sync.cc
    jobs/job_runner.cc
    marketdata/alpaca.cc
    marketdata/cached_instrument_store.cc
    marketdata/cached_provider.cc
    marketdata/candle_repo.cc
    marketdata/instrument_repo.cc
    trading/market_calendar.cc
    trading/order_math.cc
    trading/trading_repo.cc)
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
target_link_libraries(firefly_lib PRIVATE cpr::cpr PkgConfig::SODIUM)

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
