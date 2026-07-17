#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

# Unit tests are hermetic and run by default. Integration tests touch real
# Postgres or Alpaca (RUN_INTEGRATION=1). E2e tests spawn the real binary
# and need Alpaca credentials exported (set -a; source .env; set +a) plus
# RUN_E2E=1.
CTEST_ARGS=(-LE "integration|e2e")
if [[ "${RUN_INTEGRATION:-0}" == "1" ]]; then
  CTEST_ARGS=(-LE e2e)
fi
if [[ "${RUN_E2E:-0}" == "1" ]]; then
  CTEST_ARGS=()
fi
# The ${arr[@]+...} form keeps macOS bash 3.2's `set -u` happy when the
# array is empty.
ctest --test-dir build --output-on-failure ${CTEST_ARGS[@]+"${CTEST_ARGS[@]}"}
