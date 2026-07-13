#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

cmake -S . -B build -G "$GENERATOR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
