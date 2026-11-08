#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mini_checkers {

// The board is stored as 64 characters, row by row from row 0 (black's side)
// to row 7 (white's side).
inline constexpr int kBoardSize = 8;
inline constexpr char kEmptyCell = '.';
inline constexpr char kWhitePiece = 'w';
inline constexpr char kBlackPiece = 'b';
inline constexpr char kWhiteKing = 'W';
inline constexpr char kBlackKing = 'B';

enum class Side { kWhite, kBlack };

struct Cell {
  int row = 0;
  int col = 0;
  bool operator==(const Cell&) const = default;
};

struct Position {
  std::string board;
  Side turn = Side::kWhite;
  int capture_from = -1;
  // Captured pieces remain on the board as blockers until the turn ends.
  std::vector<int> captured;
};

struct Move {
  Cell from;
  Cell to;
  int captured_cell = -1;
};

struct MoveResult {
  Position position;
  int captured_cell = -1;
  bool promoted = false;
  std::optional<Side> winner;
};

std::string_view SideName(Side side);
std::optional<Side> ParseSide(std::string_view name);
Side Opponent(Side side);

int CellIndex(Cell cell);
std::string InitialBoard();

// Russian checkers: mandatory captures, flying kings, immediate promotion,
// and continuation by the same piece. Each request performs one leg.
std::vector<Move> LegalMoves(const Position& position);
std::optional<std::string_view> ValidateMove(const Position& position,
                                           Cell from, Cell to);
MoveResult ApplyMove(Position position, Cell from, Cell to);
void ClearCapturedPieces(Position& position);

}  // namespace mini_checkers
