"""Integration checks for PostgreSQL, Redis and RabbitMQ on a demo stack.

Tests create new games. Tests which stop Redis or the worker require
CHECKERS_DISRUPTIVE=1 and restore only the container they stopped.
"""

import concurrent.futures
import json
import os
import unittest

from support import compose, container_id, redis, request, run, sql, wait_for, worker_events

DISRUPTIVE = os.environ.get("CHECKERS_DISRUPTIVE") == "1"


def create_game():
    status, _, result = request("POST", "/games")
    if status != 201:
        raise AssertionError(f"creating test game returned HTTP {status}")
    return result


def outbox_empty(game):
    return sql(f"SELECT count(*) FROM outbox_events WHERE game_id = '{game}'") == "0"


def queue_depth():
    raw = compose("exec", "-T", "rabbitmq", "rabbitmqctl", "-q", "list_queues", "--online", "--timeout", "5",
                  "name", "messages", "--formatter", "json")
    return next(item["messages"] for item in json.loads(raw) if item["name"] == "game.events.quorum")


def has_event(game, event_type):
    return any(event.get("event_type") == event_type for event in worker_events(game))


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
                wait_for(lambda: has_event(game, event_type))
        wait_for(lambda: sum(event.get("event_type") == "move.made" for event in worker_events(game)) >= 2)
        self.assertEqual(sum(event.get("event_type") == "move.made" for event in worker_events(game)), 2)

    @unittest.skipUnless(DISRUPTIVE, "stops the worker; set CHECKERS_DISRUPTIVE=1 on a demo stack")
    def test_queued_events_survive_worker_stop(self):
        worker_id = container_id("event-worker")
        try:
            compose("stop", "event-worker", timeout=60)
            created = create_game()
            game = created["game_id"]
            wait_for(lambda: outbox_empty(game))
            wait_for(lambda: queue_depth() >= 1)
        finally:
            run("docker", "start", worker_id, timeout=60)
        wait_for(lambda: has_event(game, "game.created"), seconds=30)
        wait_for(lambda: queue_depth() == 0)

    @unittest.skipUnless(DISRUPTIVE, "stops Redis; set CHECKERS_DISRUPTIVE=1 on a demo stack")
    def test_redis_outage_falls_back_to_database(self):
        game = create_game()["game_id"]
        wait_for(lambda: outbox_empty(game))
        redis_id = container_id("redis")
        try:
            compose("stop", "redis", timeout=60)
            status, headers, state = request("GET", f"/games/{game}")
            self.assertEqual((status, headers["x-cache"], state["game_id"]), (200, "BYPASS", game))
        finally:
            run("docker", "start", redis_id, timeout=60)
        def cache_works():
            request("GET", f"/games/{game}")
            return request("GET", f"/games/{game}")[1].get("x-cache") == "HIT"
        wait_for(cache_works, seconds=30)


if __name__ == "__main__":
    unittest.main(verbosity=2)
