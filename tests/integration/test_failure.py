"""Проверяет перезапуск процессов и работу приложения при отказах базы, кеша и брокера.
Проверяет сохранность сообщений и доставку событий из outbox после восстановления.
Остановки сервисов выполняются только при CHECKERS_DISRUPTIVE=1.
"""

import time
import unittest

from support import (BROKERS, DISRUPTIVE, compose, create_game, healthy, inspect,
                     outbox_empty, primary, queue, request, run, sql, stopped, wait_for, worker_events)


@DISRUPTIVE
class Recovery(unittest.TestCase):
    def restart_process(self, service, seconds):
        before = inspect(service)
        if service in ("game-api", "event-worker"):
            command = [service, "sh", "-c", "kill -TERM 1"]
        elif service.startswith("postgres"):
            command = ["--user", "postgres", service, "sh", "-c", "kill -QUIT 1"]
        elif service == "redis":
            command = [service, "redis-cli", "shutdown", "nosave"]
        else:
            command = [service, "rabbitmqctl", "stop"]
        start = time.monotonic()
        run("docker", "compose", "exec", "-T", *command, check=False, timeout=seconds)

        def recovered():
            state = inspect(service)
            return (state["RestartCount"] > before["RestartCount"] and state["State"]["Running"]
                    and state["State"]["Health"]["Status"] == "healthy")

        wait_for(recovered, seconds=max(0, seconds - (time.monotonic() - start)))
        print(f"{service} auto-recovered in {time.monotonic() - start:.2f}s <= {seconds}s")

    def test_process_restarts_preserve_game(self):
        game = create_game()["game_id"]
        wait_for(lambda: worker_events(game))
        wait_for(lambda: outbox_empty(game))
        for service, limit in (("game-api", 30), ("event-worker", 30), ("redis", 30), (primary(), 60), ("rabbitmq", 60)):
            with self.subTest(service=service):
                self.assertTrue(healthy(service))
                self.restart_process(service, limit)
                self.assertEqual(request("GET", f"/games/{game}")[2]["game_id"], game)
        self.assertEqual(request("GET", "/health/ready")[0], 200)
        self.assertEqual(sql(f"SELECT status FROM games WHERE id = '{game}'"), "waiting")
        wait_for(lambda: healthy("event-worker"), seconds=30)

    def test_persistent_event_survives_worker_stop_and_broker_restart(self):
        with stopped("event-worker"):
            game = create_game()["game_id"]
            wait_for(lambda: outbox_empty(game))
            wait_for(lambda: queue()["messages"] >= 1)
            self.restart_process("rabbitmq", 60)
        wait_for(lambda: worker_events(game, "game.created"), seconds=30)
        wait_for(lambda: queue()["messages"] == 0)

    def test_redis_outage_allows_reads_and_writes(self):
        created = create_game()
        game = created["game_id"]
        wait_for(lambda: outbox_empty(game))
        with stopped("redis"):
            status, headers, state = request("GET", f"/games/{game}")
            self.assertEqual((status, headers["x-cache"], state["game_id"]), (200, "BYPASS", game))
            self.assertEqual(request("GET", "/health/ready")[0], 200)
            self.assertEqual(request("POST", f"/games/{game}/finish", created["player_token"])[0], 200)
        wait_for(lambda: request("GET", f"/games/{game}")[1].get("x-cache") == "HIT", seconds=30)
        self.assertEqual(sql(f"SELECT status FROM games WHERE id = '{game}'"), "finished")

    def test_database_outage_and_reconnection(self):
        node = primary()
        created = create_game()
        game = created["game_id"]
        self.assertEqual(request("POST", f"/games/{game}/finish", created["player_token"])[0], 200)
        with stopped(node):
            self.assertEqual(request("GET", "/health/live")[0], 200)
            self.assertEqual(request("GET", "/health/ready")[0], 503)
            self.assertEqual(request("POST", "/games")[0], 503)
        wait_for(lambda: healthy(node), seconds=60)
        wait_for(lambda: request("GET", "/health/ready")[0] == 200, seconds=30)
        self.assertEqual(sql(f"SELECT status FROM games WHERE id = '{game}'"), "finished")

    def test_quorum_loss_and_full_broker_outage(self):
        for nodes in (BROKERS[1:], BROKERS):
            with self.subTest(stopped=nodes):
                with stopped(*nodes):
                    game = create_game()["game_id"]
                    self.assertEqual(sql(f"SELECT count(*) FROM outbox_events WHERE game_id = '{game}'"), "1")
                    if len(nodes) == 3:
                        self.restart_process("game-api", 30)
                    else:
                        time.sleep(7)  # Longer than publisher confirm timeout.
                    self.assertEqual(sql(f"SELECT count(*) FROM outbox_events WHERE game_id = '{game}'"), "1")
                    self.assertEqual(request("GET", "/health/ready")[0], 200)
                wait_for(lambda: worker_events(game), seconds=60)
                wait_for(lambda: outbox_empty(game))
                wait_for(lambda: healthy("event-worker"), seconds=30)


if __name__ == "__main__":
    unittest.main(verbosity=2)
