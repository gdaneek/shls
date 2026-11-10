include(GoogleTest)

add_executable(game-rules-test game-api/game_rules_test.cc ../src/game-api/game_rules.cc)
target_include_directories(game-rules-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-rules-test PRIVATE GTest::gtest_main)

add_executable(game-api-test game-api/game_api_test.cc ../src/game-api/game_rules.cc)
target_include_directories(game-api-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-api-test PRIVATE GTest::gtest_main Drogon::Drogon)

add_executable(event-publisher-test game-api/event_publisher_test.cc)
target_include_directories(event-publisher-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(event-publisher-test PRIVATE GTest::gtest_main Drogon::Drogon rabbitmq::rabbitmq-static)

add_executable(game-api-startup-test game-api/main_test.cc
  ../src/game-api/game_api.cc ../src/game-api/game_rules.cc ../src/game-api/event_publisher.cc)
target_include_directories(game-api-startup-test PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(game-api-startup-test PRIVATE GTest::gtest_main Drogon::Drogon rabbitmq::rabbitmq-static)

add_executable(event-worker-test event-worker/main_test.cc)
target_link_libraries(event-worker-test PRIVATE GTest::gtest_main rabbitmq::rabbitmq-static)

gtest_discover_tests(game-rules-test DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(game-api-test DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(event-publisher-test DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(game-api-startup-test DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(event-worker-test DISCOVERY_MODE PRE_TEST)
