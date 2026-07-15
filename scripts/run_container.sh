#!/usr/bin/env bash
# Builds and runs firefly entirely in containers (db + app) so you can poke
# at the webservice without a local toolchain. Not for production use.
# Works with podman or docker; see scripts/engine.sh.
set -euo pipefail

cd "$(dirname "$0")/.."
source scripts/engine.sh
ENGINE="$(container_engine)"

"$ENGINE" compose up -d db
./scripts/migrate.sh
"$ENGINE" compose --profile app up -d --build app

echo "waiting for app to become healthy..."
for i in $(seq 1 30); do
  if curl -fs localhost:8080/healthz >/dev/null 2>&1; then
    echo "firefly is up: http://localhost:8080"
    exit 0
  fi
  if [[ "$i" == 30 ]]; then
    echo "error: app not healthy after 30s; check logs with:" >&2
    echo "  $ENGINE compose logs app" >&2
    exit 1
  fi
  sleep 1
done
