# Build dependencies

The root build requires CMake 3.25+, Git, a C++20 compiler, and a native build
tool such as Make or Ninja. Dependency CMake files live in this directory;
downloaded sources and build artifacts stay in the build directory.

FetchContent downloads these dependencies at pinned commits:

| Dependency | Version | Target for consumers |
| --- | --- | --- |
| Drogon (including its pinned Trantor submodule) | 1.9.11 | `Drogon::Drogon` |
| rabbitmq-c | 0.15.0 | `rabbitmq::rabbitmq-static` |
| GoogleTest (only with `BUILD_TESTING=ON`) | 1.17.0 | `GTest::gtest_main` |

Drogon is built with PostgreSQL and Redis support. Unused database backends,
dependency tests, examples, and command-line tools are disabled. The main
libraries are built statically; system dependencies may still be shared.

Install system development packages before configuring. For Debian/Ubuntu:

```bash
sudo apt-get install build-essential cmake git libssl-dev libpq-dev \
  libhiredis-dev libjsoncpp-dev uuid-dev zlib1g-dev
```

Configure and build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
```

Use `-DBUILD_TESTING=OFF` to skip GoogleTest. No application executables or
project tests exist yet; this stage only prepares their dependencies.
Future test directories should include `GoogleTest` before calling
`gtest_discover_tests()`.

The first configuration needs network access. For offline builds, prepopulate
sources and pass standard FetchContent overrides, for example
`-DFETCHCONTENT_SOURCE_DIR_DROGON=/path/to/drogon` (including its Trantor
submodule), `-DFETCHCONTENT_SOURCE_DIR_RABBITMQ_C=/path/to/rabbitmq-c`, and
`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest` when testing is enabled.
