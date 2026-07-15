#!/usr/bin/env bash
# Applies migrations/*.sql in lexical order, once each, recording applied
# files in schema_migrations. Each migration runs in a single transaction.
#
# Uses local psql with $DATABASE_URL if available, otherwise falls back to
# psql inside the compose db container (podman or docker; see engine.sh).
set -euo pipefail

cd "$(dirname "$0")/.."
source scripts/engine.sh

DATABASE_URL="${DATABASE_URL:-postgres://firefly:firefly@localhost:5432/firefly}"

if command -v psql >/dev/null 2>&1; then
  PSQL=(psql "$DATABASE_URL")
else
  ENGINE="$(container_engine)"
  PSQL=("$ENGINE" compose exec -T db psql -U firefly -d firefly)
fi

run_sql() { "${PSQL[@]}" -v ON_ERROR_STOP=1 -qAt "$@"; }

# compose up -d returns before Postgres accepts connections; wait for it.
for i in $(seq 1 30); do
  if run_sql -c "SELECT 1" >/dev/null 2>&1; then
    break
  fi
  if [[ "$i" == 30 ]]; then
    echo "error: database not reachable after 30s" >&2
    exit 1
  fi
  sleep 1
done

run_sql -c "CREATE TABLE IF NOT EXISTS schema_migrations (
    filename TEXT PRIMARY KEY,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now())"

for f in migrations/*.sql; do
  name="$(basename "$f")"
  applied="$(run_sql -c "SELECT 1 FROM schema_migrations WHERE filename = '$name'")"
  if [[ -n "$applied" ]]; then
    echo "skip  $name"
    continue
  fi
  echo "apply $name"
  {
    echo "BEGIN;"
    cat "$f"
    echo "INSERT INTO schema_migrations (filename) VALUES ('$name');"
    echo "COMMIT;"
  } | run_sql -f -
done

echo "migrations up to date"
