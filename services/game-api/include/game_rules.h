#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mini_checkers {

// The board is stored as 64 characters, row by row from row 0 (black's side)
// to row 7 (white's side).
inline constexpr int kBoardSize = 8;
inline constexpr char kEmptyCell = '.';
inline constexpr char kWhitePiece = 'w';
inline constexpr char kBlackPiece = 'b';

enum class Side { kWhite, kBlack };

struct Cell {
  int row = 0;
  int col = 0;
};

std::string_view SideName(Side side);
std::optional<Side> ParseSide(std::string_view name);
Side Opponent(Side side);

int CellIndex(Cell cell);
std::string InitialBoard();

// Simplified rules: a player moves one of their own pieces one step
// diagonally forward to an empty cell. White moves first, towards row 0.
// Returns an error description, or std::nullopt if the move is legal.
std::optional<std::string_view> ValidateMove(std::string_view board, Side side,
                                             Cell from, Cell to);
std::string ApplyMove(std::string board, Cell from, Cell to);

}  // namespace mini_checkers
