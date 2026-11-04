add_executable(game-api src/main.cc)

target_link_libraries(game-api PRIVATE Drogon::Drogon)
