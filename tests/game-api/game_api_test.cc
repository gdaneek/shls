#include <gtest/gtest.h>

#include <stdexcept>

#include "shls/game_api.h"

using namespace mini_checkers;

TEST(GameApi, RejectsMissingOrEmptySchema) {
  GameApi api(nullptr, nullptr, 60);
  EXPECT_THROW(api.CreateSchema("/dev/null/schema.sql"), std::runtime_error);
  EXPECT_THROW(api.CreateSchema("/dev/null"), std::runtime_error);
}
