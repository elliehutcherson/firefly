#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_PRESET="${1:-dev}"
if [[ "$BUILD_PRESET" != "dev" && "$BUILD_PRESET" != "asan" ]]; then
  echo "usage: $0 [dev|asan]" >&2
  exit 2
fi

if [[ -z "${BUILD_JOBS:-}" ]]; then
  if command -v getconf >/dev/null 2>&1; then
    BUILD_JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  elif command -v sysctl >/dev/null 2>&1; then
    BUILD_JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
  fi
  BUILD_JOBS="${BUILD_JOBS:-1}"
fi
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

echo "building and testing preset $BUILD_PRESET with $BUILD_JOBS parallel jobs"

cmake --preset "$BUILD_PRESET"
cmake --build --preset "$BUILD_PRESET" --parallel "$BUILD_JOBS"

# Unit tests are hermetic and run by default. Integration tests touch real
# Postgres or Alpaca (RUN_INTEGRATION=1). E2e tests spawn the real binary
# and need Alpaca credentials exported (set -a; source .env; set +a) plus
# RUN_E2E=1.
if [[ "${RUN_E2E:-0}" == "1" ]]; then
  ctest --test-dir "build/$BUILD_PRESET" --output-on-failure --parallel "$BUILD_JOBS"
elif [[ "${RUN_INTEGRATION:-0}" == "1" ]]; then
  ctest --test-dir "build/$BUILD_PRESET" --output-on-failure \
      --parallel "$BUILD_JOBS" -LE e2e
else
  ctest --preset "$BUILD_PRESET" --parallel "$BUILD_JOBS"
fi
