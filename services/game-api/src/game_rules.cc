#include "game_rules.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace mini_checkers {
namespace {

constexpr std::array<Cell, 4> kDirections = {{{-1, -1}, {-1, 1}, {1, -1}, {1, 1}}};

bool OnBoard(Cell cell) {
  return cell.row >= 0 && cell.row < 8 && cell.col >= 0 && cell.col < 8;
}

Cell Step(Cell cell, Cell direction) {
  return {cell.row + direction.row, cell.col + direction.col};
}

bool Owns(Side side, char piece) {
  return side == Side::kWhite ? piece == 'w' || piece == 'W'
                              : piece == 'b' || piece == 'B';
}

bool Captured(const Position& position, int index) {
  return std::find(position.captured.begin(), position.captured.end(), index) !=
         position.captured.end();
}

std::vector<Move> GenerateMoves(const Position& position, bool captures) {
  std::vector<Move> moves;
  if (position.board.size() != 64) return moves;
  for (int index = 0; index < 64; ++index) {
    if (position.capture_from >= 0 && index != position.capture_from) continue;
    const char piece = position.board[index];
    if (!Owns(position.turn, piece) || Captured(position, index)) continue;
    const Cell from{index / 8, index % 8};
    const bool king = piece == 'W' || piece == 'B';
    for (Cell direction : kDirections) {
      Cell next = Step(from, direction);
      if (!captures) {
        if (!king && direction.row != (position.turn == Side::kWhite ? -1 : 1)) continue;
        while (OnBoard(next) && position.board[CellIndex(next)] == '.') {
          moves.push_back({from, next, -1});
          if (!king) break;
          next = Step(next, direction);
        }
        continue;
      }
      if (king) {
        while (OnBoard(next) && position.board[CellIndex(next)] == '.') {
          next = Step(next, direction);
        }
      }
      if (!OnBoard(next)) continue;
      const int victim = CellIndex(next);
      if (!Owns(Opponent(position.turn), position.board[victim]) || Captured(position, victim)) continue;
      next = Step(next, direction);
      while (OnBoard(next) && position.board[CellIndex(next)] == '.') {
        moves.push_back({from, next, victim});
        if (!king) break;
        next = Step(next, direction);
      }
    }
  }
  return moves;
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

int CellIndex(Cell cell) { return cell.row * 8 + cell.col; }

std::string InitialBoard() {
  std::string board(64, '.');
  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
      if ((row + col) % 2 == 0) continue;
      if (row < 3) board[CellIndex({row, col})] = 'b';
      if (row > 4) board[CellIndex({row, col})] = 'w';
    }
  }
  return board;
}

std::vector<Move> LegalMoves(const Position& position) {
  auto captures = GenerateMoves(position, true);
  if (!captures.empty() || position.capture_from >= 0) return captures;
  return GenerateMoves(position, false);
}

std::optional<std::string_view> ValidateMove(const Position& position,
                                           Cell from, Cell to) {
  if (!OnBoard(from) || !OnBoard(to)) return "coordinates must be in the range 0-7";
  if (position.board.size() != 64) return "invalid board";
  if (!Owns(position.turn, position.board[CellIndex(from)]) ||
      Captured(position, CellIndex(from))) return "the source cell does not contain your piece";
  if (position.capture_from >= 0 && position.capture_from != CellIndex(from)) {
    return "continue capturing with the same piece";
  }
  if (position.board[CellIndex(to)] != '.') return "the target cell is not empty";
  const auto moves = LegalMoves(position);
  for (const auto& move : moves) {
    if (move.from == from && move.to == to) return std::nullopt;
  }
  if (!moves.empty() && moves.front().captured_cell >= 0) return "capture is mandatory";
  return "illegal move";
}

void ClearCapturedPieces(Position& position) {
  for (int index : position.captured) position.board.at(index) = '.';
  position.captured.clear();
  position.capture_from = -1;
}

MoveResult ApplyMove(Position position, Cell from, Cell to) {
  const auto moves = LegalMoves(position);
  const auto move = std::find_if(moves.begin(), moves.end(), [&](const auto& value) {
    return value.from == from && value.to == to;
  });
  if (move == moves.end()) throw std::invalid_argument("illegal move");
  const Side mover = position.turn;
  const char piece = position.board[CellIndex(from)];
  const bool promoted = (piece == 'w' && to.row == 0) || (piece == 'b' && to.row == 7);
  position.board[CellIndex(to)] = promoted ? (mover == Side::kWhite ? 'W' : 'B') : piece;
  position.board[CellIndex(from)] = '.';
  const int victim = move->captured_cell;
  if (victim >= 0) {
    position.captured.push_back(victim);
    position.capture_from = CellIndex(to);
    // Promotion is already applied: a man reaching the last rank continues
    // this same capture sequence with a flying king's rights.
    if (!LegalMoves(position).empty()) {
      return {std::move(position), victim, promoted, std::nullopt};
    }
  }
  ClearCapturedPieces(position);
  position.turn = Opponent(mover);
  const auto winner = LegalMoves(position).empty() ? std::optional<Side>(mover) : std::nullopt;
  return {std::move(position), victim, promoted, winner};
}

}  // namespace mini_checkers
