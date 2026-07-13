# Build stage. Expects the repo checked out with submodules
# (git clone --recurse-submodules).
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    libpq-dev libcurl4-openssl-dev libsodium-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target firefly

# Runtime stage: just the binary and the shared libraries it links.
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 libcurl4 libsodium23 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 firefly

COPY --from=build /src/build/bin/firefly /usr/local/bin/firefly

USER firefly
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/firefly"]
