#include "shls/game_rules.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace mini_checkers;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  std::string("line ") + std::to_string(__LINE__) + ": " + #condition); } while (false)

Position Setup(std::initializer_list<std::pair<Cell, char>> pieces,
               Side turn = Side::kWhite) {
  Position position{std::string(64, '.'), turn};
  for (const auto& [cell, piece] : pieces) position.board[CellIndex(cell)] = piece;
  return position;
}

bool Allows(const Position& position, Cell from, Cell to) {
  return !ValidateMove(position, from, to);
}

int main() {
  try {
    auto initial = Position{InitialBoard()};
    CHECK(LegalMoves(initial).size() == 7);
    CHECK(std::count(initial.board.begin(), initial.board.end(), 'w') == 12);
    CHECK(std::count(initial.board.begin(), initial.board.end(), 'b') == 12);
    CHECK(Allows(initial, {5, 0}, {4, 1}));
    CHECK(!Allows(initial, {-1, 0}, {4, 1}));
    CHECK(!Allows(initial, {5, 0}, {8, 1}));
    CHECK(!Allows(initial, {5, 0}, {3, 2}));
    std::cout << "PASS initial position and coordinate validation\n";

    auto simple = Setup({{{5, 2}, 'w'}, {{0, 1}, 'b'}});
    CHECK(Allows(simple, {5, 2}, {4, 1}));
    CHECK(!Allows(simple, {5, 2}, {6, 1}));
    auto quiet = ApplyMove(simple, {5, 2}, {4, 1});
    CHECK(quiet.position.turn == Side::kBlack);
    CHECK(!quiet.winner);
    std::cout << "PASS men move forward, turn changes\n";

    auto mandatory = Setup({{{5, 0}, 'w'}, {{5, 4}, 'w'}, {{4, 1}, 'b'}, {{0, 1}, 'b'}});
    CHECK(!Allows(mandatory, {5, 4}, {4, 3}));
    CHECK(Allows(mandatory, {5, 0}, {3, 2}));
    auto backward = Setup({{{3, 2}, 'w'}, {{4, 3}, 'b'}, {{0, 1}, 'b'}});
    CHECK(Allows(backward, {3, 2}, {5, 4}));
    auto back = ApplyMove(backward, {3, 2}, {5, 4});
    CHECK(back.position.board[CellIndex({4, 3})] == '.');
    auto black_back = Setup({{{4, 3}, 'b'}, {{3, 2}, 'w'}, {{7, 0}, 'w'}}, Side::kBlack);
    CHECK(Allows(black_back, {4, 3}, {2, 1}));
    std::cout << "PASS compulsory capture, both colors capture backwards\n";

    auto chain = Setup({{{5, 0}, 'w'}, {{7, 6}, 'w'}, {{4, 1}, 'b'}, {{2, 3}, 'b'}, {{0, 7}, 'b'}});
    auto first = ApplyMove(chain, {5, 0}, {3, 2});
    CHECK(first.position.turn == Side::kWhite);
    CHECK(first.position.capture_from == CellIndex({3, 2}));
    CHECK(first.position.captured.size() == 1);
    CHECK(first.position.board[CellIndex({4, 1})] == 'b');
    CHECK(!Allows(first.position, {7, 6}, {6, 5}));
    CHECK(!Allows(first.position, {3, 2}, {5, 0}));
    auto second = ApplyMove(first.position, {3, 2}, {1, 4});
    CHECK(second.position.turn == Side::kBlack);
    CHECK(second.position.capture_from == -1);
    CHECK(second.position.captured.empty());
    CHECK(second.position.board[CellIndex({4, 1})] == '.');
    CHECK(second.position.board[CellIndex({2, 3})] == '.');
    std::cout << "PASS same-piece chain, deferred removal, no recapture\n";

    auto branches = Setup({{{5, 2}, 'w'}, {{4, 1}, 'b'}, {{4, 3}, 'b'}, {{2, 5}, 'b'}});
    CHECK(Allows(branches, {5, 2}, {3, 0}));
    CHECK(Allows(branches, {5, 2}, {3, 4}));
    CHECK(ApplyMove(branches, {5, 2}, {3, 0}).position.turn == Side::kBlack);
    CHECK(ApplyMove(branches, {5, 2}, {3, 4}).position.turn == Side::kWhite);
    std::cout << "PASS branch choice without maximum-capture rule\n";

    auto king = Setup({{{5, 2}, 'W'}, {{0, 7}, 'b'}});
    CHECK(Allows(king, {5, 2}, {2, 5}));
    CHECK(Allows(king, {5, 2}, {7, 4}));
    CHECK(!Allows(king, {5, 2}, {2, 4}));
    king.board[CellIndex({4, 3})] = 'w';
    CHECK(!Allows(king, {5, 2}, {2, 5}));
    auto king_capture = Setup({{{7, 0}, 'W'}, {{5, 2}, 'b'}, {{3, 4}, 'b'}});
    CHECK(Allows(king_capture, {7, 0}, {4, 3}));
    CHECK(!Allows(king_capture, {7, 0}, {2, 5}));
    auto king_step = ApplyMove(king_capture, {7, 0}, {4, 3});
    CHECK(Allows(king_step.position, {4, 3}, {2, 5}));
    CHECK(Allows(king_step.position, {4, 3}, {0, 7}));
    std::cout << "PASS flying kings, one victim per jump, landing choices\n";

    auto turkish = Setup({{{4, 3}, 'W'}, {{3, 4}, 'b'}, {{1, 6}, 'b'}, {{5, 2}, 'b'}});
    auto turkish_step = ApplyMove(turkish, {4, 3}, {2, 5});
    CHECK(Allows(turkish_step.position, {2, 5}, {0, 7}));
    CHECK(!Allows(turkish_step.position, {2, 5}, {6, 1}));
    CHECK(!Allows(turkish_step.position, {2, 5}, {7, 0}));
    std::cout << "PASS captured pieces block a king's return path (Turkish strike)\n";

    auto crown = Setup({{{2, 1}, 'w'}, {{1, 2}, 'b'}, {{3, 6}, 'b'}});
    auto crowned = ApplyMove(crown, {2, 1}, {0, 3});
    CHECK(crowned.promoted);
    CHECK(crowned.position.board[3] == 'W');
    CHECK(crowned.position.turn == Side::kWhite);
    CHECK(Allows(crowned.position, {0, 3}, {4, 7}));
    auto crown_end = ApplyMove(crowned.position, {0, 3}, {4, 7});
    CHECK(crown_end.winner == Side::kWhite);
    auto quiet_crown = ApplyMove(Setup({{{1, 2}, 'w'}, {{3, 6}, 'b'}}), {1, 2}, {0, 3});
    CHECK(quiet_crown.promoted && quiet_crown.position.turn == Side::kBlack);
    CHECK(quiet_crown.position.capture_from == -1);
    auto black_crown = ApplyMove(Setup({{{6, 1}, 'b'}, {{4, 7}, 'w'}}, Side::kBlack), {6, 1}, {7, 0});
    CHECK(black_crown.promoted && black_crown.position.board[56] == 'B');
    std::cout << "PASS immediate promotion; capture continues as king, quiet promotion ends turn\n";

    auto last = ApplyMove(Setup({{{5, 0}, 'w'}, {{4, 1}, 'b'}}), {5, 0}, {3, 2});
    CHECK(last.winner == Side::kWhite);
    auto blocked = Setup({{{7, 0}, 'w'}, {{7, 2}, 'w'}, {{5, 0}, 'w'},
                          {{5, 2}, 'w'}, {{4, 3}, 'w'}, {{5, 4}, 'w'}, {{6, 1}, 'b'}});
    CHECK(ApplyMove(blocked, {5, 4}, {4, 5}).winner == Side::kWhite);
    std::cout << "PASS victory by elimination or blocking every opposing move\n";

    auto loop = Setup({{{6, 3}, 'W'}, {{5, 4}, 'b'}, {{3, 4}, 'b'}, {{3, 2}, 'b'}, {{5, 2}, 'b'}});
    loop = ApplyMove(loop, {6, 3}, {4, 5}).position;
    loop = ApplyMove(loop, {4, 5}, {2, 3}).position;
    loop = ApplyMove(loop, {2, 3}, {4, 1}).position;
    auto loop_end = ApplyMove(loop, {4, 1}, {6, 3});
    CHECK(loop_end.position.board[CellIndex({6, 3})] == 'W');
    CHECK(loop_end.winner == Side::kWhite);
    std::cout << "PASS revisiting an empty square in a capture chain is legal\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
