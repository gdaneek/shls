#include <gtest/gtest.h>

#define main game_api_entry_point
#include "../../src/game-api/main.cc"
#undef main

TEST(GameApiStartup, QuotesPostgresConnectionValues) {
  EXPECT_EQ(PgQuote("plain"), "'plain'");
  EXPECT_EQ(PgQuote("a'b\\c"), "'a\\'b\\\\c'");
}

TEST(GameApiStartup, ReadsEnvironmentDefaults) {
  constexpr char key[] = "MINI_CHECKERS_UNIT_TEST_CONFIG";
  unsetenv(key);
  EXPECT_EQ(Env(key, "default"), "default");
  setenv(key, "configured", 1);
  EXPECT_EQ(RequiredEnv(key), "configured");
  unsetenv(key);
  EXPECT_THROW(RequiredEnv(key), std::runtime_error);
}
