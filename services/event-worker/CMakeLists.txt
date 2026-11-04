add_executable(event-worker src/main.cc)

target_link_libraries(event-worker PRIVATE rabbitmq::rabbitmq-static)
