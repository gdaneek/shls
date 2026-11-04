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
- Docker Compose stack with Game API, PostgreSQL, Redis, and RabbitMQ.
- `.env.example` with the required secrets.
- `scripts/test-startup.sh` startup check.
- Startup instructions in the README.
- Shared root Dockerfile with a BuildKit cache for dependencies and object files.

### Fixed

- Drogon PostgreSQL/Redis support check that always failed because the flags are set in Drogon's directory scope.
