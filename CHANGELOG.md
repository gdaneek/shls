# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Sphinx architecture guide with C4 diagrams and a README-based landing page.
- GitHub Actions C++ build and tests for pushes and pull requests to `dev`, with a README status badge.
- GoogleTest unit checks for game rules, API helpers, publisher, and both service entry points, registered with CTest.
- MIT license.
- Initial project description in Russian.
- Common C++ and CMake ignore rules.
- Google-style clang-format configuration.
- Project changelog.
- Root C++20 CMake build with pinned Drogon, rabbitmq-c, and optional GoogleTest dependencies.
- Game API placeholder service (`GET /hello`) on C++20/Drogon with a multi-stage Dockerfile.
- Docker Compose stack with Game API, PostgreSQL, Redis, and RabbitMQ.
- `.env.example` with the required secrets.
- `scripts/test-startup.sh` startup check.
- Startup instructions in the README.
- Shared root Dockerfile with a BuildKit cache for dependencies and object files.
- Event Worker service that consumes game events from RabbitMQ and logs them as JSON lines.
- Game API endpoints to create, join, read, move in, and finish games with simplified rules and per-player tokens.
- PostgreSQL storage, Redis cache-aside reads with invalidation on writes, and RabbitMQ events with publisher confirms.
- `scripts/test-integration.sh` check of the database, cache, and broker flows; the startup check covers Event Worker.
- API description in the README.
- Network segments `public`, `data`, and `messaging` with a fixed IP plan; `data` and `messaging` are internal.
- nftables bridge firewall for the internal segments with explicit ALLOW rules and a logged default DENY.
- `scripts/test-network.sh` check of allowed, firewall-denied, and segmentation-denied connections.
- Network segments and firewall description in the README.
- Service restart policies, API liveness/readiness, Worker heartbeat healthcheck, and PostgreSQL outbox for event delivery after recovery.
- PostgreSQL dump/restore scripts, seven-day retention, and a daily systemd user backup timer.
- Failure-recovery checks with measured restart times, and backup restore/retention/safety checks.
- Reliability targets and backup operation instructions in the README.
- SLI/SLO acceptance checks for correct reads, p95 latency, and timely event processing.
- Documented SLI/SLO acceptance objectives and successful firewall-enabled verification.
- Browser game interface with an interactive board, tab-local player credentials, spectator mode, and manual refresh.
- Unprivileged frontend container with a same-origin API proxy, a private backend port, and explicit frontend firewall rules.
- Frontend HTTP and Firefox browser checks, including two-player moves and tab isolation.
- Browser game controls and frontend startup instructions in the README.
- Automatic game-state polling with hidden-tab pause, bounded error backoff, serialized requests, and stale-version protection.
- Browser coverage for automatic synchronization, preserved selection/input, polling recovery, and terminal-state pause.
- Updated playing instructions for automatic board refresh.
- Russian-checkers captures and flying kings with durable continuation state, immediate promotion, and automatic victory.
- Server-driven legal-move highlights, forced capture selection, king markers, and deferred capture removal in the web interface.
- Standalone C++ rule checks plus API/browser coverage of captures, promotion, persistence, concurrency, and legacy cache compatibility.
- Playing instructions for mandatory captures and kings in the README.
- Canonical PostgreSQL schema SQL file loaded by the API at startup and included in its Docker image.
- Disposable-database checks for schema initialization, idempotency, legacy upgrades, and image resource consistency.
- Firewall fault demonstration that blocks required API-to-PostgreSQL traffic and verifies errors, rollback, and recovery.
- Native PostgreSQL streaming standby with separate storage, replication credentials, and explicit database network rules.
- Guarded PostgreSQL promotion and rewind/rejoin, API redirection, scoped cache invalidation, and role-aware backups/restores.
- PostgreSQL crash-failover tests and role-aware schema, network, recovery, game, and browser regression checks.
- PostgreSQL replication setup and explicit failover operation commands in the README.
- Three-node RabbitMQ quorum replication, separate persistent storage, and automatic publisher/consumer reconnection.
- Guarded single-node RabbitMQ upgrade that preserves the existing cookie, queue, and volume.
- Broker leader-crash, consumer reconnection, quorum-loss, and cluster-aware regression checks.
- RabbitMQ cluster setup and failover demonstration commands in the README.

### Changed

- Consolidated local build outputs and default test screenshots under `build/`.
- C++ service images now build and run GoogleTest checks in the Docker build stage.
- Consolidated project headers in `shls/`, implementation sources in `src/`, tests in `tests/`, and infrastructure in `infra/`.

### Fixed

- Drogon PostgreSQL/Redis support check that always failed because the flags are set in Drogon's directory scope.
- README focused on installation, playing, and basic operation instead of internal network and failover details.
- RabbitMQ peer startup no longer waits on seed health, avoiding metadata-recovery deadlocks during cluster recreation.
