# syntax=docker/dockerfile:1
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git make \
    libssl-dev libpq-dev libhiredis-dev libjsoncpp-dev uuid-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY third-party/ third-party/
COPY services/ services/
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
RUN cmake --build /build --target game-api --parallel 2

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libssl3 libpq5 libhiredis0.14 libjsoncpp25 libuuid1 zlib1g \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/services/game-api/game-api /usr/local/bin/game-api
WORKDIR /app
USER 10001:10001
EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/game-api"]
