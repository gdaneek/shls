"""Проверяет путь партии через PostgreSQL, Redis и очередь событий.
Проверяет кеширование, авторизацию, отклонение неверных ходов и защиту от одновременных записей.
"""

import concurrent.futures
import unittest

from support import create_game, outbox_empty, redis, request, sql, wait_for, worker_events


class Flows(unittest.TestCase):
    def test_database_cache_events_and_concurrent_moves(self):
        created = create_game()
        game, white = created["game_id"], created["player_token"]
        self.assertEqual(created["side"], "white")
        wait_for(lambda: outbox_empty(game))
        self.assertEqual(redis("EXISTS", f"game:{game}"), "0")
        status, headers, state = request("GET", f"/games/{game}")
        self.assertEqual((status, headers["x-cache"], state["status"]), (200, "MISS", "waiting"))
        self.assertEqual(redis("EXISTS", f"game:{game}"), "1")
        self.assertGreater(int(redis("TTL", f"game:{game}")), 0)
        self.assertLessEqual(int(redis("TTL", f"game:{game}")), 60)
        self.assertEqual(request("GET", f"/games/{game}")[1]["x-cache"], "HIT")

        status, _, joined = request("POST", f"/games/{game}/join")
        self.assertEqual((status, joined["side"]), (200, "black"))
        black = joined["player_token"]
        self.assertEqual(redis("EXISTS", f"game:{game}"), "0")
        self.assertEqual(request("POST", f"/games/{game}/join")[0], 409)
        wait_for(lambda: outbox_empty(game))
        status, headers, state = request("GET", f"/games/{game}")
        self.assertEqual((status, headers["x-cache"], state["status"]), (200, "MISS", "active"))

        move = {"from": [5, 0], "to": [4, 1]}
        self.assertEqual(request("POST", f"/games/{game}/moves", body=move)[0], 401)
        self.assertEqual(request("POST", f"/games/{game}/moves", "not-a-player", move)[0], 403)
        self.assertEqual(request("POST", f"/games/{game}/moves", black, {"from": [2, 1], "to": [3, 0]})[0], 409)
        self.assertEqual(request("POST", f"/games/{game}/moves", white, {"from": [5, 0], "to": [3, 2]})[0], 422)
        self.assertEqual(request("POST", f"/games/{game}/moves", white, {"from": "a1"})[0], 400)
        status, _, state = request("POST", f"/games/{game}/moves", white, move)
        self.assertEqual((status, state["current_turn"], state["board"][4]), (200, "black", ".w......"))
        self.assertEqual(redis("EXISTS", f"game:{game}"), "0")
        wait_for(lambda: outbox_empty(game))
        status, headers, state = request("GET", f"/games/{game}")
        self.assertEqual((status, headers["x-cache"], state["version"]), (200, "MISS", 3))
        self.assertEqual(sql(f"SELECT move_no, player_side, from_cell, to_cell FROM moves WHERE game_id = '{game}'"), "1|white|40|33")
        self.assertEqual(sql(f"SELECT count(*) FROM game_players WHERE token_hash IN ('{white}', '{black}')"), "0")

        def move_black(target):
            return request("POST", f"/games/{game}/moves", black, {"from": [2, 1], "to": target})[0]

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            statuses = list(pool.map(move_black, ([3, 0], [3, 2])))
        self.assertEqual(sorted(statuses), [200, 409])
        self.assertEqual(sql(f"SELECT count(*) FROM moves WHERE game_id = '{game}'"), "2")
        status, _, state = request("POST", f"/games/{game}/finish", white)
        self.assertEqual((status, state["status"], state["winner"]), (200, "finished", "black"))
        self.assertEqual(request("POST", f"/games/{game}/moves", black, {"from": [2, 3], "to": [3, 4]})[0], 409)
        self.assertEqual(sql(f"SELECT status, winner FROM games WHERE id = '{game}'"), "finished|black")
        for event_type in ("game.created", "player.joined", "move.made", "game.finished"):
            with self.subTest(event=event_type):
                wait_for(lambda: worker_events(game, event_type))
        # Delivery is at-least-once: count distinct events, not repeated log lines.
        wait_for(lambda: len({event["event_id"] for event in worker_events(game, "move.made")}) >= 2)
        self.assertEqual(len({event["event_id"] for event in worker_events(game, "move.made")}), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
