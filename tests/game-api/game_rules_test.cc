#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

#include "shls/game_rules.h"

using namespace mini_checkers;

TEST(GameRules, InitialPosition) {
  const Position position{InitialBoard()};
  ASSERT_EQ(position.board.size(), 64);
  EXPECT_EQ(std::count(position.board.begin(), position.board.end(), 'w'), 12);
  EXPECT_EQ(std::count(position.board.begin(), position.board.end(), 'b'), 12);
  EXPECT_EQ(LegalMoves(position).size(), 7);
}

TEST(GameRules, MoveAndTurn) {
  const Position position{InitialBoard()};
  EXPECT_FALSE(ValidateMove(position, {5, 0}, {4, 1}));
  EXPECT_TRUE(ValidateMove(position, {5, 0}, {3, 2}));
  EXPECT_THROW(ApplyMove(position, {5, 0}, {3, 2}), std::invalid_argument);
  const auto move = ApplyMove(position, {5, 0}, {4, 1});
  EXPECT_EQ(move.position.board[CellIndex({5, 0})], '.');
  EXPECT_EQ(move.position.board[CellIndex({4, 1})], 'w');
  EXPECT_EQ(move.position.turn, Side::kBlack);
}

TEST(GameRules, MandatoryCaptureAndChain) {
  Position position{std::string(64, '.'), Side::kWhite};
  position.board[CellIndex({5, 0})] = 'w';
  position.board[CellIndex({7, 6})] = 'w';
  position.board[CellIndex({4, 1})] = 'b';
  position.board[CellIndex({2, 3})] = 'b';
  position.board[CellIndex({0, 7})] = 'b';
  EXPECT_TRUE(ValidateMove(position, {7, 6}, {6, 5}));
  const auto first = ApplyMove(position, {5, 0}, {3, 2});
  EXPECT_EQ(first.position.turn, Side::kWhite);
  EXPECT_EQ(first.position.capture_from, CellIndex({3, 2}));
  EXPECT_EQ(first.position.board[CellIndex({4, 1})], 'b');
  const auto second = ApplyMove(first.position, {3, 2}, {1, 4});
  EXPECT_EQ(second.position.turn, Side::kBlack);
  EXPECT_EQ(second.position.board[CellIndex({4, 1})], '.');
  EXPECT_EQ(second.position.board[CellIndex({2, 3})], '.');
}

TEST(GameRules, PromotionAndVictory) {
  Position position{std::string(64, '.'), Side::kWhite};
  position.board[CellIndex({2, 1})] = 'w';
  position.board[CellIndex({1, 2})] = 'b';
  position.board[CellIndex({3, 6})] = 'b';
  const auto promoted = ApplyMove(position, {2, 1}, {0, 3});
  EXPECT_TRUE(promoted.promoted);
  EXPECT_EQ(promoted.position.board[CellIndex({0, 3})], 'W');
  EXPECT_EQ(promoted.position.turn, Side::kWhite);
  const auto last = ApplyMove(promoted.position, {0, 3}, {4, 7});
  ASSERT_TRUE(last.winner);
  EXPECT_EQ(*last.winner, Side::kWhite);
}

TEST(GameRules, Sides) {
  EXPECT_EQ(ParseSide("white"), Side::kWhite);
  EXPECT_FALSE(ParseSide("invalid"));
  EXPECT_EQ(SideName(Opponent(Side::kWhite)), "black");
}
