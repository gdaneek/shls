include(GoogleTest)

add_executable(game-rules-test game-api/game_rules_test.cc ../src/game-api/game_rules.cc)
target_include_directories(game-rules-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-rules-test PRIVATE GTest::gtest_main)
enable_project_coverage(game-rules-test)

add_executable(game-api-test game-api/game_api_test.cc
  ../src/game-api/game_api.cc ../src/game-api/game_rules.cc ../src/game-api/event_publisher.cc)
target_include_directories(game-api-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-api-test PRIVATE GTest::gtest_main Drogon::Drogon rabbitmq::rabbitmq-static)
target_compile_definitions(game-api-test PRIVATE SHLS_SCHEMA_PATH="${PROJECT_SOURCE_DIR}/infra/postgres/schema.sql")
enable_project_coverage(game-api-test)

gtest_discover_tests(game-rules-test DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(game-api-test DISCOVERY_MODE PRE_TEST)
