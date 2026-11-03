include(FetchContent)

# Older nested dependencies (Trantor) must also honor scoped option overrides.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# Keep dependency test suites disabled without changing the project's option.
set(MINI_CHECKERS_BUILD_TESTING "${BUILD_TESTING}")
set(BUILD_TESTING OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL 1.1.1 REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(ZLIB REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/drogon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/rabbitmq-c.cmake")

if(MINI_CHECKERS_BUILD_TESTING)
  include("${CMAKE_CURRENT_LIST_DIR}/googletest.cmake")
endif()
