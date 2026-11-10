# Images of both custom services. Build context: repository root.
#   docker build --target game-api .
#   docker build --target event-worker .
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git \
    libhiredis-dev libjsoncpp-dev libpq-dev libssl-dev uuid-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY third-party ./third-party
COPY shls ./shls
COPY src ./src
COPY tests ./tests

# The cache mount keeps downloaded dependencies and object files between
# builds, so a code change does not re-clone and rebuild Drogon.
RUN --mount=type=cache,target=/build,sharing=locked \
    cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    && cmake --build /build --parallel 4 \
    && ctest --test-dir /build --output-on-failure \
    && mkdir -p /src/build \
    && cp /build/game-api /build/event-worker /src/build/

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libhiredis0.14 libjsoncpp25 libpq5 libssl3 libuuid1 zlib1g \
    && rm -rf /var/lib/apt/lists/*

USER 10001:10001

FROM runtime AS game-api
COPY --from=build /src/build/game-api /usr/local/bin/game-api
COPY infra/postgres/schema.sql /usr/local/share/mini-checkers/schema.sql
ENV POSTGRES_SCHEMA_FILE=/usr/local/share/mini-checkers/schema.sql
EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/game-api"]

FROM runtime AS event-worker
COPY --from=build /src/build/event-worker /usr/local/bin/event-worker
ENTRYPOINT ["/usr/local/bin/event-worker"]
