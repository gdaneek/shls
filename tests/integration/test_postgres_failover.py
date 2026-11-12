"""Проверяет аварийное завершение PostgreSQL primary и управляемое переключение на standby.
Проверяет чтение и запись после переключения, очистку кеша и возврат старого узла через pg_rewind.
Запуск с CHECKERS_DISRUPTIVE=1 меняет роли узлов и адрес базы в конфигурации.
"""

import time
import unittest

from support import (DISRUPTIVE, ROOT, compose, inspect, interruptible, joined_game,
                     primary, redis, request, run, sql, wait_for)


@DISRUPTIVE
class PostgresFailover(unittest.TestCase):
    def test_primary_crash_promotion_and_rejoin(self):
        old = primary()
        candidate = "postgres-standby" if old == "postgres" else "postgres"
        self.assertEqual(sql("SELECT pg_is_in_recovery()", node=candidate), "t")
        game, white, black = joined_game()
        self.assertEqual(request("POST", f"/games/{game}/moves", white,
                                 {"from": [5, 0], "to": [4, 1]})[0], 200)
        lsn = sql("SELECT pg_current_wal_flush_lsn()", node=old)
        wait_for(lambda: sql(f"SELECT pg_last_wal_replay_lsn() >= '{lsn}'::pg_lsn AND EXISTS "
                            f"(SELECT 1 FROM games WHERE id = '{game}' AND version = 3)",
                            node=candidate) == "t", seconds=30)
        sentinel = f"test:postgres-failover:{game}"
        complete = False
        with interruptible():
            try:
                redis("SET", sentinel, "preserve")
                self.assertEqual(request("GET", f"/games/{game}")[0], 200)
                run(str(ROOT / "infra/backups/backup.sh"), timeout=300)
                old_id = inspect(old)["Id"]
                run("docker", "update", "--restart=no", old_id)
                start = time.monotonic()
                compose("kill", "-s", "SIGKILL", old)
                state = inspect(old)["State"]
                self.assertFalse(state["Running"])
                self.assertEqual(state["ExitCode"], 137)
                # These are the operational tools under test, not shell test wrappers.
                run(str(ROOT / "infra/postgres/promote-postgres.sh"), candidate, timeout=120)
                wait_for(lambda: request("GET", "/health/ready")[0] == 200, seconds=60)
                self.assertEqual(primary(), candidate)
                self.assertFalse(inspect(old)["State"]["Running"])
                self.assertEqual(redis("GET", sentinel), "preserve")
                self.assertEqual(redis("EXISTS", f"game:{game}"), "0")
                status, headers, state = request("GET", f"/games/{game}")
                self.assertEqual((status, headers["x-cache"], state["version"]), (200, "MISS", 3))
                status, _, state = request("POST", f"/games/{game}/moves", black,
                                           {"from": [2, 1], "to": [3, 0]})
                self.assertEqual((status, state["version"]), (200, 4))
                self.assertEqual(sql(f"SELECT count(*) FROM moves WHERE game_id = '{game}'", node=candidate), "2")
                elapsed = time.monotonic() - start
                self.assertLessEqual(elapsed, 60)
                print(f"{old} -> {candidate}: read/write restored in {elapsed:.2f}s <= 60s")
                run(str(ROOT / "infra/postgres/rejoin-postgres.sh"), old, timeout=300)
                self.assertEqual(sql("SELECT pg_is_in_recovery()", node=old), "t")
                self.assertEqual(sql(f"SELECT version FROM games WHERE id = '{game}'", node=old), "4")
                self.assertEqual(primary(), candidate)
                self.assertTrue(inspect(old)["State"]["Running"])
                run(str(ROOT / "infra/backups/backup.sh"), timeout=300)
                complete = True
            finally:
                try:
                    redis("DEL", sentinel)
                finally:
                    if not complete:
                        print("Inspect PostgreSQL roles before recovery; do not blindly restart the old primary.")


if __name__ == "__main__":
    unittest.main(verbosity=2)
