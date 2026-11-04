#include "game_rules.h"

#include <cstdlib>

namespace mini_checkers {
namespace {

bool OnBoard(Cell cell) {
  return cell.row >= 0 && cell.row < kBoardSize && cell.col >= 0 &&
         cell.col < kBoardSize;
}

char PieceOf(Side side) {
  return side == Side::kWhite ? kWhitePiece : kBlackPiece;
}

}  // namespace

std::string_view SideName(Side side) {
  return side == Side::kWhite ? "white" : "black";
}

std::optional<Side> ParseSide(std::string_view name) {
  if (name == "white") return Side::kWhite;
  if (name == "black") return Side::kBlack;
  return std::nullopt;
}

Side Opponent(Side side) {
  return side == Side::kWhite ? Side::kBlack : Side::kWhite;
}

int CellIndex(Cell cell) { return cell.row * kBoardSize + cell.col; }

std::string InitialBoard() {
  std::string board(kBoardSize * kBoardSize, kEmptyCell);
  for (int row = 0; row < kBoardSize; ++row) {
    for (int col = 0; col < kBoardSize; ++col) {
      if ((row + col) % 2 == 0) continue;
      if (row < 3) board[CellIndex({row, col})] = kBlackPiece;
      if (row > 4) board[CellIndex({row, col})] = kWhitePiece;
    }
  }
  return board;
}

std::optional<std::string_view> ValidateMove(std::string_view board, Side side,
                                             Cell from, Cell to) {
  if (!OnBoard(from) || !OnBoard(to)) {
    return "coordinates must be in the range 0-7";
  }
  if (board[CellIndex(from)] != PieceOf(side)) {
    return "the source cell does not contain your piece";
  }
  if (board[CellIndex(to)] != kEmptyCell) {
    return "the target cell is not empty";
  }
  const int forward = side == Side::kWhite ? -1 : 1;
  if (to.row - from.row != forward || std::abs(to.col - from.col) != 1) {
    return "a piece moves one step diagonally forward";
  }
  return std::nullopt;
}

std::string ApplyMove(std::string board, Cell from, Cell to) {
  board[CellIndex(to)] = board[CellIndex(from)];
  board[CellIndex(from)] = kEmptyCell;
  return board;
}

}  // namespace mini_checkers
