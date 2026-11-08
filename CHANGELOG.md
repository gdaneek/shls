# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- MIT license.
- Initial project description in Russian.
- Common C++ and CMake ignore rules.
- Google-style clang-format configuration.
- Project changelog.
- Root C++20 CMake build with pinned Drogon, rabbitmq-c, and optional GoogleTest dependencies.
- Game API placeholder service (`GET /hello`) on C++20/Drogon with a multi-stage Dockerfile.
- Docker Compose stack with Game API, PostgreSQL, Redis, and RabbitMQ (assignment 2).
- `.env.example` with the required secrets.
- `scripts/test-startup.sh` startup check.
- Startup instructions in the README.
- Shared root Dockerfile with a BuildKit cache for dependencies and object files.
- Event Worker service that consumes game events from RabbitMQ and logs them as JSON lines.
- Game API endpoints to create, join, read, move in, and finish games with simplified rules and per-player tokens.
- PostgreSQL storage, Redis cache-aside reads with invalidation on writes, and RabbitMQ events with publisher confirms (assignment 3).
- `scripts/test-integration.sh` check of the database, cache, and broker flows; the startup check covers Event Worker.
- API description in the README.
- Network segments `public`, `data`, and `messaging` with a fixed IP plan; `data` and `messaging` are internal.
- nftables bridge firewall for the internal segments with explicit ALLOW rules and a logged default DENY (assignment 4).
- `scripts/test-network.sh` check of allowed, firewall-denied, and segmentation-denied connections.

### Fixed

- Drogon PostgreSQL/Redis support check that always failed because the flags are set in Drogon's directory scope.
