#!/usr/bin/env python3
"""Проверяет через API обязательные взятия, цепочки, дамки, победу и конкурирующие ходы.
Позиции подготавливаются SQL-запросами только в новых тестовых партиях.
Проверка сохранения цепочки после рестарта API включается через CHECKERS_DISRUPTIVE=1.
"""
import concurrent.futures
import json
import unittest

from support import (DISRUPTIVE, compose, healthy, joined_game, outbox_empty,
                     redis, request, seed_position, sql, wait_for, worker_events)


def fixture(pieces, turn="white"):
    game, white, black = joined_game()
    seed_position(game, pieces, turn)
    return game, white, black


class Captures(unittest.TestCase):
    def move(self, game, token, start, target, status=200):
        actual, _, state = request("POST", f"/games/{game}/moves", token, {"from": start, "to": target})
        self.assertEqual(actual, status, state)
        return state

    def test_mandatory_and_backward_capture(self):
        game, white, _ = fixture([(3, 2, "w"), (5, 0, "w"), (4, 3, "b"), (0, 1, "b")])
        self.move(game, white, [5, 0], [4, 1], 422)
        state = self.move(game, white, [3, 2], [5, 4])
        self.assertEqual(state["board"][4][3], ".")
        self.assertEqual(state["current_turn"], "black")
        game, _, black = fixture([(4, 3, "b"), (3, 2, "w"), (7, 0, "w")], "black")
        self.move(game, black, [4, 3], [2, 1])

    @DISRUPTIVE
    def test_chain_survives_api_restart(self):
        game, white, black = fixture([(5, 0, "w"), (7, 6, "w"), (4, 1, "b"), (2, 3, "b"), (0, 7, "b")])
        state = self.move(game, white, [5, 0], [3, 2])
        self.assertEqual(state["capture_from"], [3, 2])
        self.assertEqual(state["captured_cells"], [[4, 1]])
        self.assertEqual(state["board"][4][1], "b")
        self.move(game, white, [7, 6], [6, 5], 422)
        self.move(game, white, [3, 2], [2, 1], 422)
        self.move(game, black, [0, 7], [1, 6], 409)
        self.assertEqual(sql(f"SELECT capture_from FROM games WHERE id = '{game}'"), "26")
        compose("restart", "game-api", timeout=60)
        wait_for(lambda: healthy("game-api"), seconds=60)
        state = request("GET", f"/games/{game}")[2]
        self.assertEqual(state["capture_from"], [3, 2])
        self.move(game, white, [3, 2], [5, 0], 422)
        state = self.move(game, white, [3, 2], [1, 4])
        self.assertEqual(state["current_turn"], "black")
        self.assertIsNone(state["capture_from"])
        self.assertEqual(state["captured_cells"], [])
        self.assertEqual(state["board"][4][1], ".")
        self.assertEqual(state["board"][2][3], ".")
        self.assertEqual(sql(f"SELECT string_agg(captured_cell::text, ',' ORDER BY move_no) FROM moves WHERE game_id = '{game}'"), "33,19")

    def test_flying_king_and_turkish_blocker(self):
        game, white, _ = fixture([(4, 3, "W"), (3, 4, "b"), (1, 6, "b"), (5, 2, "b")])
        state = self.move(game, white, [4, 3], [2, 5])
        self.assertEqual(state["board"][3][4], "b")
        self.move(game, white, [2, 5], [6, 1], 422)
        state = self.move(game, white, [2, 5], [0, 7])
        self.assertEqual(state["board"][5][2], "b")
        self.assertEqual(state["captured_cells"], [])
        self.assertEqual(state["current_turn"], "black")

    def test_promotion_in_chain_and_win_event(self):
        game, white, black = fixture([(2, 1, "w"), (1, 2, "b"), (3, 6, "b")])
        state = self.move(game, white, [2, 1], [0, 3])
        self.assertEqual(state["board"][0][3], "W")
        self.assertEqual(state["capture_from"], [0, 3])
        self.move(game, white, [0, 3], [1, 4], 422)
        state = self.move(game, white, [0, 3], [4, 7])
        self.assertEqual((state["status"], state["winner"]), ("finished", "white"))
        self.assertEqual(state["legal_moves"], [])
        self.move(game, black, [1, 2], [2, 3], 409)
        self.assertEqual(sql(f"SELECT promoted FROM moves WHERE game_id = '{game}' ORDER BY move_no LIMIT 1"), "t")
        finished = wait_for(lambda: worker_events(game, "game.finished"))
        self.assertEqual(json.loads(finished[0]["payload"])["data"]["winner"], "white")

    def test_quiet_promotion_and_distant_king_move(self):
        game, _, black = fixture([(6, 1, "b"), (4, 7, "w")], "black")
        state = self.move(game, black, [6, 1], [7, 0])
        self.assertEqual(state["board"][7][0], "B")
        self.assertEqual(state["current_turn"], "white")
        game, white, _ = fixture([(5, 2, "W"), (0, 1, "b")])
        state = self.move(game, white, [5, 2], [2, 5])
        self.assertEqual(state["board"][2][5], "W")

    def test_shorter_capture_branch_is_allowed(self):
        game, white, _ = fixture([(5, 2, "w"), (4, 1, "b"), (4, 3, "b"), (2, 5, "b")])
        state = self.move(game, white, [5, 2], [3, 0])
        self.assertEqual(state["current_turn"], "black")
        self.assertIsNone(state["capture_from"])

    def test_resign_during_chain_clears_captured_pieces(self):
        game, white, _ = fixture([(5, 0, "w"), (4, 1, "b"), (2, 3, "b")])
        self.move(game, white, [5, 0], [3, 2])
        status, _, state = request("POST", f"/games/{game}/finish", white)
        self.assertEqual(status, 200)
        self.assertEqual(state["winner"], "black")
        self.assertEqual(state["board"][4][1], ".")
        self.assertIsNone(state["capture_from"])

    def test_concurrent_chain_legs_only_one_commits(self):
        game, white, _ = fixture([(5, 0, "w"), (4, 1, "b"), (2, 1, "b"), (2, 3, "b")])
        self.move(game, white, [5, 0], [3, 2])
        def attempt(target):
            return request("POST", f"/games/{game}/moves", white, {"from": [3, 2], "to": target})[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            statuses = list(pool.map(attempt, [[1, 0], [1, 4]]))
        self.assertEqual(sorted(statuses), [200, 409])
        self.assertEqual(sql(f"SELECT count(*) FROM moves WHERE game_id = '{game}'"), "2")

    def test_win_when_opponent_has_no_legal_move(self):
        game, white, _ = fixture([(7, 0, "w"), (7, 2, "w"), (5, 0, "w"),
                                   (5, 2, "w"), (4, 3, "w"), (5, 4, "w"), (6, 1, "b")])
        state = self.move(game, white, [5, 4], [4, 5])
        self.assertEqual((state["status"], state["winner"]), ("finished", "white"))

    def test_old_cache_format_is_not_served(self):
        game, _, _ = fixture([(5, 2, "w"), (0, 1, "b")])
        wait_for(lambda: outbox_empty(game))
        _, _, state = request("GET", f"/games/{game}")
        state.pop("rules_version")
        state.pop("legal_moves")
        redis("SET", f"game:{game}", json.dumps(state), "EX", "60")
        _, _, state = request("GET", f"/games/{game}")
        self.assertEqual(state["rules_version"], 2)
        self.assertTrue(state["legal_moves"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
