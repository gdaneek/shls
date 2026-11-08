add_executable(game-api
  src/main.cc
  src/game_api.cc
  src/game_rules.cc
  src/event_publisher.cc
)

target_include_directories(game-api PRIVATE include)
target_link_libraries(game-api PRIVATE Drogon::Drogon rabbitmq::rabbitmq-static)

# Standalone domain checks: no DB, browser or extra testing dependencies.
add_executable(game-rules-test tests/game_rules_test.cc src/game_rules.cc)
target_include_directories(game-rules-test PRIVATE include)
if(BUILD_TESTING)
  add_test(NAME game-rules COMMAND game-rules-test)
endif()
