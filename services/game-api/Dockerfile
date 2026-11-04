# Build context: repository root (see docker-compose.yml).
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git \
    libhiredis-dev libjsoncpp-dev libpq-dev libssl-dev uuid-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY third-party ./third-party
COPY services/game-api ./services/game-api

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /build --target game-api --parallel "$(nproc)"

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libhiredis0.14 libjsoncpp25 libpq5 libssl3 libuuid1 zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/services/game-api/game-api /usr/local/bin/game-api

USER 10001:10001
EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/game-api"]
