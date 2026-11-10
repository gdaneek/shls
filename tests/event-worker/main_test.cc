#include <gtest/gtest.h>

#define main event_worker_entry_point
#include "../../src/event-worker/main.cc"
#undef main

TEST(EventWorker, EscapesJsonLogFields) {
  EXPECT_EQ(JsonEscape("hello\n\"world\"\\"), "hello\\n\\\"world\\\"\\\\");
}

TEST(EventWorker, ReadsMessageProperties) {
  amqp_basic_properties_t properties{};
  properties._flags = AMQP_BASIC_TYPE_FLAG;
  properties.type = amqp_cstring_bytes("game.created");
  EXPECT_EQ(Property(properties, AMQP_BASIC_TYPE_FLAG, properties.type), "game.created");
  EXPECT_EQ(Property(properties, AMQP_BASIC_MESSAGE_ID_FLAG, amqp_empty_bytes), "");
}

TEST(EventWorker, LogsProcessedEvent) {
  amqp_envelope_t envelope{};
  envelope.message.properties._flags = AMQP_BASIC_TYPE_FLAG | AMQP_BASIC_MESSAGE_ID_FLAG;
  envelope.message.properties.type = amqp_cstring_bytes("game.created");
  envelope.message.properties.message_id = amqp_cstring_bytes("event-1");
  envelope.message.body = amqp_cstring_bytes("{}");
  testing::internal::CaptureStdout();
  ProcessEvent(envelope, 1);
  const auto output = testing::internal::GetCapturedStdout();
  EXPECT_NE(output.find("\"event_type\":\"game.created\""), std::string::npos);
  EXPECT_NE(output.find("\"event_id\":\"event-1\""), std::string::npos);
}
