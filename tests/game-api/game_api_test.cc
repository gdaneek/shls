#include <gtest/gtest.h>

#include "../../src/game-api/game_api.cc"

using namespace mini_checkers;

TEST(GameApi, ValidatesGameIdsAndCoordinates) {
  EXPECT_EQ(NormalizeGameId("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"),
            "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  EXPECT_FALSE(NormalizeGameId("invalid"));
  const auto cell = ParseCell(ParseJson("[5,0]"));
  ASSERT_TRUE(cell);
  EXPECT_EQ(*cell, (Cell{5, 0}));
  EXPECT_FALSE(ParseCell(ParseJson("[5]")));
}

TEST(GameApi, SerializesActiveGameAndLegalMoves) {
  const GameState game{"id", "active", InitialBoard(), "white", "", 1};
  const auto json = ToJson(game);
  EXPECT_EQ(json["game_id"].asString(), "id");
  EXPECT_EQ(json["board"].size(), 8);
  EXPECT_EQ(json["legal_moves"].size(), 7);
  EXPECT_TRUE(json["winner"].isNull());
}

TEST(GameApi, IssuesAndHashesPlayerTokens) {
  const auto first = NewPlayerToken();
  const auto second = NewPlayerToken();
  EXPECT_EQ(first.size(), 64);
  EXPECT_NE(first, second);
  EXPECT_EQ(HashToken(first).size(), 64);
  EXPECT_NE(HashToken(first), first);
}

TEST(GameApi, RejectsMissingSchema) {
  GameApi api(nullptr, nullptr, 60);
  EXPECT_THROW(api.CreateSchema("/dev/null/schema.sql"), std::runtime_error);
}
