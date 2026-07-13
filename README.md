# firefly

A paper-trading web app: $10,000 of virtual cash, real (delayed) market data,
shorts, movers, and opt-in leaderboards.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and
[docs/STYLE.md](docs/STYLE.md) for the C++ style guide.

## Setup

```sh
git clone --recurse-submodules <repo-url>
sudo apt install pkg-config libpq-dev libcurl4-openssl-dev libsodium-dev
```

## Build & test

```sh
./scripts/build_and_test.sh
```

## Run locally

```sh
docker compose up -d db      # start Postgres
./scripts/migrate.sh         # apply migrations/*.sql
./build/bin/firefly          # listens on 127.0.0.1:8080
curl localhost:8080/healthz
```

Configuration is via environment variables: `FIREFLY_BIND`, `FIREFLY_PORT`,
`DATABASE_URL` (see `src/common/config.h` for defaults).

## Dependencies

C++ dependencies are git submodules under `include/`, pinned to release tags
and built from source via CMake. Only stable-ABI C libraries (libpq, libcurl,
libsodium) come from the system.
