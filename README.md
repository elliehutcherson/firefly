# firefly

A paper-trading web app: $10,000 of virtual cash, real (delayed) market data,
shorts, movers, and opt-in leaderboards.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and
[docs/STYLE.md](docs/STYLE.md) for the C++ style guide.

The monorepo is split into `backend/` for the C++ service and `frontend/`
for the Svelte/TypeScript client. Shared build, deployment, and developer
tooling remains at the repository root.

## Setup

```sh
git clone --recurse-submodules <repo-url>
```

Install the system libraries (everything else is vendored as a submodule):

```sh
# Debian/Ubuntu
sudo apt install pkg-config libpq-dev libcurl4-openssl-dev libsodium-dev

# macOS (Homebrew)
brew install cmake pkg-config libpq curl libsodium
```

## Build & test

```sh
./scripts/build_and_test.sh
./scripts/build_and_test.sh asan
```

The default run is hermetic. Set `RUN_INTEGRATION=1` to include tests against
local Postgres and the live Alpaca API; unavailable services self-skip.
`RUN_E2E=1` is reserved for running all categories, including future full-app
end-to-end tests.

Build and type-check the frontend separately:

```sh
npm ci --prefix frontend
npm run check --prefix frontend
npm run build --prefix frontend
```

## Run locally

Podman and Docker both work; scripts auto-detect (podman preferred, override
with `CONTAINER_ENGINE=docker|podman`). With podman, install the
`docker-compose` provider too (`brew install docker-compose`) — not
podman-compose. The podman machine defaults to 2 GB of memory, which OOMs
the containerized C++ build; give it more before using run_container.sh:
`podman machine set --memory 8192` (while stopped).

```sh
podman compose up -d db      # start Postgres (or: docker compose up -d db)
./scripts/migrate.sh         # apply backend/migrations/*.sql
./build/dev/bin/firefly      # listens on 127.0.0.1:8080
curl localhost:8080/healthz
```

Configuration is via environment variables: `FIREFLY_BIND`, `FIREFLY_PORT`,
`DATABASE_URL`, `FIREFLY_DB_POOL_SIZE`, and
`FIREFLY_DB_ACQUIRE_TIMEOUT_MS` (see `backend/src/common/config.h` for defaults).

### Run fully in containers

No local toolchain needed — builds the app image, starts Postgres, applies
migrations, and runs the service in a container:

```sh
./scripts/run_container.sh
curl localhost:8080/healthz
```

## Dependencies

C++ dependencies are git submodules under `backend/third_party/`, pinned to release tags
and built from source via CMake. Only stable-ABI C libraries (libpq, libcurl,
libsodium) come from the system.
