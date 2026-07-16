include(GoogleTest)

# Shared dependencies for test targets. Fakes and matchers remain headers so
# tests pay only for the support they include.
add_library(firefly_test_support INTERFACE)
target_include_directories(firefly_test_support INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(firefly_test_support INTERFACE
    firefly_lib
    absl::status_matchers
    GTest::gmock
    GTest::gtest_main)

# Keep target declaration uniform and attach an execution-category label to
# every discovered GoogleTest case. Source paths mirror src/ under unit/.
function(firefly_add_test name source label)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE firefly_test_support)
  gtest_discover_tests(${name} PROPERTIES LABELS ${label})
endfunction()

firefly_add_test(auth_handlers_test unit/api/auth_handlers_test.cc unit)
firefly_add_test(market_handlers_test unit/api/market_handlers_test.cc unit)
firefly_add_test(rate_limiter_test unit/api/rate_limiter_test.cc unit)
firefly_add_test(server_test unit/api/server_test.cc unit)
firefly_add_test(status_mapping_test unit/api/status_mapping_test.cc unit)

firefly_add_test(crypto_test unit/auth/crypto_test.cc unit)
firefly_add_test(session_repo_test unit/auth/session_repo_test.cc unit)
firefly_add_test(turnstile_test unit/auth/turnstile_test.cc unit)
firefly_add_test(user_repo_test unit/auth/user_repo_test.cc unit)

firefly_add_test(config_test unit/common/config_test.cc unit)
firefly_add_test(http_test unit/common/http_test.cc unit)
firefly_add_test(money_test unit/common/money_test.cc unit)

firefly_add_test(row_reader_test unit/db/row_reader_test.cc unit)

firefly_add_test(daily_bar_sync_test unit/jobs/daily_bar_sync_test.cc unit)
firefly_add_test(job_runner_test unit/jobs/job_runner_test.cc unit)

firefly_add_test(alpaca_test unit/marketdata/alpaca_test.cc unit)
firefly_add_test(cached_provider_test unit/marketdata/cached_provider_test.cc unit)
firefly_add_test(candle_repo_test unit/marketdata/candle_repo_test.cc unit)
firefly_add_test(instrument_repo_test unit/marketdata/instrument_repo_test.cc unit)

# Real Postgres; self-skips when the configured database is unreachable.
firefly_add_test(db_test integration/db/db_test.cc integration)

# Real Alpaca API; self-skips without API credentials.
firefly_add_test(alpaca_live_test
    integration/marketdata/alpaca_live_test.cc integration)
