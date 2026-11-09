cmake_minimum_required(VERSION 3.25)

project(MiniCheckers DESCRIPTION "Secure microservice-based checkers prototype" LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use cmake -S . -B build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_subdirectory(third-party)

add_executable(game-api
  src/game-api/main.cc
  src/game-api/game_api.cc
  src/game-api/game_rules.cc
  src/game-api/event_publisher.cc
)
target_include_directories(game-api PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-api PRIVATE Drogon::Drogon rabbitmq::rabbitmq-static)

add_executable(event-worker src/event-worker/main.cc)
target_link_libraries(event-worker PRIVATE rabbitmq::rabbitmq-static)

# Standalone domain checks: no DB, browser or extra testing dependencies.
add_executable(game-rules-test tests/game-api/game_rules_test.cc src/game-api/game_rules.cc)
target_include_directories(game-rules-test PRIVATE "${PROJECT_SOURCE_DIR}")
if(BUILD_TESTING)
  add_test(NAME game-rules COMMAND game-rules-test)
endif()
