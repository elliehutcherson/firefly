#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

cmake -S . -B build -G "$GENERATOR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

# Tests labeled e2e (real Alpaca API, real Postgres) are excluded unless
# RUN_E2E=1; they also self-skip when their credentials/database are absent.
CTEST_ARGS=(-LE e2e)
if [[ "${RUN_E2E:-0}" == "1" ]]; then
  CTEST_ARGS=()
fi
ctest --test-dir build --output-on-failure "${CTEST_ARGS[@]}"
