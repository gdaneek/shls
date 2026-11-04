# Images of the custom services. Build context: repository root.
#   docker build --target game-api .
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git \
    libhiredis-dev libjsoncpp-dev libpq-dev libssl-dev uuid-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY third-party ./third-party
COPY services ./services

# The cache mount keeps downloaded dependencies and object files between
# builds, so a code change does not re-clone and rebuild Drogon.
RUN --mount=type=cache,target=/build,sharing=locked \
    cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /build --target game-api --parallel "$(nproc)" \
    && mkdir -p /out \
    && cp /build/services/game-api/game-api /out/

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libhiredis0.14 libjsoncpp25 libpq5 libssl3 libuuid1 zlib1g \
    && rm -rf /var/lib/apt/lists/*

USER 10001:10001

FROM runtime AS game-api
COPY --from=build /out/game-api /usr/local/bin/game-api
EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/game-api"]

