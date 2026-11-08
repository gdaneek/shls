#!/usr/bin/env python3
"""Russian-checkers API checks on the local demo stack (Python stdlib).

Each test creates NEW games; SQL only arranges those test fixtures, never a
user's existing game. One test restarts game-api to verify a durable chain.
"""
import concurrent.futures
import json
from pathlib import Path
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


def compose(*args):
    return subprocess.check_output(["docker", "compose", *args], cwd=ROOT, stderr=subprocess.DEVNULL).decode().strip()


def sql(query):
    return compose("exec", "-T", "postgres", "psql", "-X", "-v", "ON_ERROR_STOP=1",
                   "-U", "mini_checkers", "-d", "mini_checkers", "-tAc", query)


def request(method, path, token=None, body=None):
    args = ["exec", "-T", "game-api", "curl", "-sS", "--max-time", "8", "-X", method,
            "-w", "\n%{http_code}", f"http://127.0.0.1:8000{path}"]
    if token:
        args += ["-H", "X-Player-Token: " + token]
    if body is not None:
        args += ["-H", "Content-Type: application/json", "-d", json.dumps(body)]
    payload, status = compose(*args).rsplit("\n", 1)
    return int(status), json.loads(payload)


def fixture(pieces, turn="white"):
    _, white = request("POST", "/games")
    game = white["game_id"]
    _, black = request("POST", f"/games/{game}/join")
    board = ["."] * 64
    for row, col, piece in pieces:
        assert (row + col) % 2 == 1 and piece in "wbWB"
        board[row * 8 + col] = piece
    sql(f"UPDATE games SET board = '{''.join(board)}', current_turn = '{turn}', "
        f"version = version + 1 WHERE id = '{game}'")
    compose("exec", "-T", "redis", "redis-cli", "DEL", f"game:{game}")
    return game, white["player_token"], black["player_token"]


class Captures(unittest.TestCase):
    def move(self, game, token, start, target, status=200):
        actual, state = request("POST", f"/games/{game}/moves", token, {"from": start, "to": target})
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
        compose("restart", "game-api")
        compose("up", "-d", "--wait", "--wait-timeout", "60", "game-api")
        state = request("GET", f"/games/{game}")[1]
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
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            records = [json.loads(line) for line in compose("logs", "--no-log-prefix", "event-worker").splitlines() if game in line]
            finished = [record for record in records if record.get("event_type") == "game.finished"]
            if finished:
                self.assertEqual(json.loads(finished[0]["payload"])["data"]["winner"], "white")
                break
            time.sleep(0.5)
        else:
            self.fail("worker did not process automatic game.finished")

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
        status, state = request("POST", f"/games/{game}/finish", white)
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
        deadline = time.monotonic() + 15
        while sql(f"SELECT count(*) FROM outbox_events WHERE game_id = '{game}'") != "0":
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.2)
        _, state = request("GET", f"/games/{game}")
        state.pop("rules_version")
        state.pop("legal_moves")
        compose("exec", "-T", "redis", "redis-cli", "SET", f"game:{game}", json.dumps(state), "EX", "60")
        _, state = request("GET", f"/games/{game}")
        self.assertEqual(state["rules_version"], 2)
        self.assertTrue(state["legal_moves"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
