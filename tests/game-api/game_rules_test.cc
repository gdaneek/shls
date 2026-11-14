#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include "shls/game_rules.h"

using namespace mini_checkers;

Position PositionWith(std::initializer_list<std::pair<Cell, char>> pieces,
                      Side turn = Side::kWhite) {
  Position position{std::string(64, '.'), turn};
  for (const auto& [cell, piece] : pieces) position.board[CellIndex(cell)] = piece;
  return position;
}

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

TEST(GameRules, RejectsInvalidMoves) {
  const Position initial{InitialBoard()};
  EXPECT_TRUE(ValidateMove(initial, {-1, 0}, {4, 1}));
  EXPECT_TRUE(ValidateMove(initial, {5, 0}, {8, 1}));
  EXPECT_TRUE(ValidateMove(initial, {0, 1}, {1, 0}));
  EXPECT_TRUE(ValidateMove(initial, {5, 0}, {6, 1}));
  const Position invalid{"short"};
  EXPECT_TRUE(LegalMoves(invalid).empty());
  EXPECT_TRUE(ValidateMove(invalid, {5, 0}, {4, 1}));
}

TEST(GameRules, BlackMovesForward) {
  Position position{InitialBoard(), Side::kBlack};
  EXPECT_FALSE(ValidateMove(position, {2, 1}, {3, 0}));
  const auto isolated = PositionWith({{{3, 2}, 'b'}, {{0, 1}, 'w'}}, Side::kBlack);
  EXPECT_TRUE(ValidateMove(isolated, {3, 2}, {2, 1}));
  const auto move = ApplyMove(position, {2, 1}, {3, 0});
  EXPECT_EQ(move.position.board[CellIndex({3, 0})], 'b');
  EXPECT_EQ(move.position.turn, Side::kWhite);
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
  EXPECT_EQ(first.position.captured.size(), 1);
  EXPECT_TRUE(ValidateMove(first.position, {7, 6}, {6, 5}));
  EXPECT_TRUE(ValidateMove(first.position, {3, 2}, {5, 0}));
  const auto second = ApplyMove(first.position, {3, 2}, {1, 4});
  EXPECT_EQ(second.position.turn, Side::kBlack);
  EXPECT_EQ(second.position.capture_from, -1);
  EXPECT_TRUE(second.position.captured.empty());
  EXPECT_EQ(second.position.board[CellIndex({4, 1})], '.');
  EXPECT_EQ(second.position.board[CellIndex({2, 3})], '.');
}

TEST(GameRules, MenCaptureBackwards) {
  const auto white = PositionWith({{{3, 2}, 'w'}, {{4, 3}, 'b'}, {{0, 1}, 'b'}});
  EXPECT_FALSE(ValidateMove(white, {3, 2}, {5, 4}));
  const auto black = PositionWith({{{4, 3}, 'b'}, {{3, 2}, 'w'}, {{7, 0}, 'w'}},
                                  Side::kBlack);
  EXPECT_FALSE(ValidateMove(black, {4, 3}, {2, 1}));
}

TEST(GameRules, PlayerChoosesBetweenCaptureBranches) {
  const auto position = PositionWith({{{5, 2}, 'w'}, {{4, 1}, 'b'},
                                      {{4, 3}, 'b'}, {{2, 5}, 'b'}});
  EXPECT_FALSE(ValidateMove(position, {5, 2}, {3, 0}));
  EXPECT_FALSE(ValidateMove(position, {5, 2}, {3, 4}));
  EXPECT_EQ(ApplyMove(position, {5, 2}, {3, 0}).position.turn, Side::kBlack);
  EXPECT_EQ(ApplyMove(position, {5, 2}, {3, 4}).position.turn, Side::kWhite);
}

TEST(GameRules, FlyingKingAndCapture) {
  auto position = PositionWith({{{5, 2}, 'W'}, {{0, 7}, 'b'}});
  EXPECT_FALSE(ValidateMove(position, {5, 2}, {2, 5}));
  EXPECT_FALSE(ValidateMove(position, {5, 2}, {7, 4}));
  position.board[CellIndex({4, 3})] = 'w';
  EXPECT_TRUE(ValidateMove(position, {5, 2}, {2, 5}));
  const auto captures = PositionWith({{{7, 0}, 'W'}, {{5, 2}, 'b'}, {{3, 4}, 'b'}});
  const auto first = ApplyMove(captures, {7, 0}, {4, 3});
  EXPECT_FALSE(ValidateMove(first.position, {4, 3}, {2, 5}));
  EXPECT_FALSE(ValidateMove(first.position, {4, 3}, {0, 7}));
}

TEST(GameRules, CapturedPiecesBlockKing) {
  const auto position = PositionWith({{{4, 3}, 'W'}, {{3, 4}, 'b'},
                                      {{1, 6}, 'b'}, {{5, 2}, 'b'}});
  const auto move = ApplyMove(position, {4, 3}, {2, 5});
  EXPECT_FALSE(ValidateMove(move.position, {2, 5}, {0, 7}));
  EXPECT_TRUE(ValidateMove(move.position, {2, 5}, {6, 1}));
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

TEST(GameRules, BlackPromotion) {
  const auto position = PositionWith({{{6, 1}, 'b'}, {{4, 7}, 'w'}}, Side::kBlack);
  const auto move = ApplyMove(position, {6, 1}, {7, 0});
  EXPECT_TRUE(move.promoted);
  EXPECT_EQ(move.position.board[CellIndex({7, 0})], 'B');
  EXPECT_EQ(move.position.turn, Side::kWhite);
}

TEST(GameRules, QuietPromotionEndsTurn) {
  const auto position = PositionWith({{{1, 2}, 'w'}, {{3, 6}, 'b'}});
  const auto move = ApplyMove(position, {1, 2}, {0, 3});
  EXPECT_TRUE(move.promoted);
  EXPECT_EQ(move.position.board[CellIndex({0, 3})], 'W');
  EXPECT_EQ(move.position.turn, Side::kBlack);
  EXPECT_EQ(move.position.capture_from, -1);
}

TEST(GameRules, CaptureCanReturnToStartingCell) {
  auto position = PositionWith({{{6, 3}, 'W'}, {{5, 4}, 'b'}, {{3, 4}, 'b'},
                                {{3, 2}, 'b'}, {{5, 2}, 'b'}});
  position = ApplyMove(position, {6, 3}, {4, 5}).position;
  position = ApplyMove(position, {4, 5}, {2, 3}).position;
  position = ApplyMove(position, {2, 3}, {4, 1}).position;
  const auto move = ApplyMove(position, {4, 1}, {6, 3});
  EXPECT_EQ(move.position.board[CellIndex({6, 3})], 'W');
  ASSERT_TRUE(move.winner);
  EXPECT_EQ(*move.winner, Side::kWhite);
}

TEST(GameRules, VictoryWhenOpponentCannotMove) {
  const auto position = PositionWith({{{7, 0}, 'w'}, {{7, 2}, 'w'}, {{5, 0}, 'w'},
                                      {{5, 2}, 'w'}, {{4, 3}, 'w'}, {{5, 4}, 'w'},
                                      {{6, 1}, 'b'}});
  const auto move = ApplyMove(position, {5, 4}, {4, 5});
  ASSERT_TRUE(move.winner);
  EXPECT_EQ(*move.winner, Side::kWhite);
}

TEST(GameRules, ClearsCapturedPieces) {
  auto position = PositionWith({{{3, 2}, 'w'}, {{4, 1}, 'b'}, {{2, 3}, 'b'}});
  position.capture_from = CellIndex({3, 2});
  position.captured = {CellIndex({4, 1}), CellIndex({2, 3})};
  ClearCapturedPieces(position);
  EXPECT_EQ(position.board[CellIndex({4, 1})], '.');
  EXPECT_EQ(position.board[CellIndex({2, 3})], '.');
  EXPECT_TRUE(position.captured.empty());
  EXPECT_EQ(position.capture_from, -1);
}

TEST(GameRules, Sides) {
  EXPECT_EQ(ParseSide("white"), Side::kWhite);
  EXPECT_EQ(ParseSide("black"), Side::kBlack);
  EXPECT_FALSE(ParseSide("invalid"));
  EXPECT_EQ(SideName(Opponent(Side::kWhite)), "black");
  EXPECT_EQ(SideName(Opponent(Side::kBlack)), "white");
}
