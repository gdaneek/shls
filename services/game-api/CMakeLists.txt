add_executable(game-api
  src/main.cc
  src/game_api.cc
  src/game_rules.cc
  src/event_publisher.cc
)

target_include_directories(game-api PRIVATE include)
target_link_libraries(game-api PRIVATE Drogon::Drogon rabbitmq::rabbitmq-static)
