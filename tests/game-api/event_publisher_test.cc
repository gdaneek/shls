#include <gtest/gtest.h>

#include "../../src/game-api/event_publisher.cc"

using namespace mini_checkers;

TEST(EventPublisher, RecognizesSuccessfulReplies) {
  amqp_rpc_reply_t reply{};
  reply.reply_type = AMQP_RESPONSE_NORMAL;
  EXPECT_TRUE(IsOk(reply));
  reply.reply_type = AMQP_RESPONSE_LIBRARY_EXCEPTION;
  EXPECT_FALSE(IsOk(reply));
}

TEST(EventPublisher, EncodesMessageBytes) {
  const std::string body = "game.created";
  const auto bytes = ToBytes(body);
  EXPECT_EQ(bytes.len, body.size());
  EXPECT_EQ(std::string(static_cast<const char*>(bytes.bytes), bytes.len), body);
}

TEST(EventPublisher, CanBeDestroyedBeforeStart) {
  EventPublisher publisher({.host = "rabbitmq", .queue = "events"}, nullptr, nullptr);
}
