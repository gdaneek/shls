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

option(ENABLE_COVERAGE "Collect coverage for project C++ targets with GCC" OFF)
if(ENABLE_COVERAGE AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "ENABLE_COVERAGE requires GCC and gcov")
endif()

function(enable_project_coverage target)
  if(ENABLE_COVERAGE)
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

add_subdirectory(third-party)

add_executable(game-api
  src/game-api/main.cc
  src/game-api/game_api.cc
  src/game-api/game_rules.cc
  src/game-api/event_publisher.cc
)
target_include_directories(game-api PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-api PRIVATE Drogon::Drogon rabbitmq::rabbitmq-static)
enable_project_coverage(game-api)

add_executable(event-worker src/event-worker/main.cc)
target_link_libraries(event-worker PRIVATE rabbitmq::rabbitmq-static)
enable_project_coverage(event-worker)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
