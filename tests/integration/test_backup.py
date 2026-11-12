"""Exercise the real backup/restore tools against a new, disposable database."""

import os
from pathlib import Path
import tempfile
import time
import unittest
import uuid

from support import ROOT, compose, joined_game, primary, request, run, sql


class Backups(unittest.TestCase):
    def test_restore_safety_and_retention(self):
        node = primary()
        game, white, _ = joined_game()
        self.assertEqual(request("POST", f"/games/{game}/moves", white,
                                 {"from": [5, 0], "to": [4, 1]})[0], 200)
        query = f"SELECT status, board, current_turn, version FROM games WHERE id = '{game}'"
        expected = sql(query, node=node)
        backup = str(ROOT / "infra/backups/backup.sh")
        restore = str(ROOT / "infra/backups/restore.sh")
        database = "mini_checkers_restore_" + uuid.uuid4().hex
        (ROOT / "backups").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="test-", dir=ROOT / "backups") as directory:
            start = time.monotonic()
            archive = run(backup, env={"BACKUP_DIR": directory}, timeout=300).stdout.strip()
            run(restore, archive, database, timeout=300)
            self.addCleanup(compose, "exec", "-T", node, "dropdb", "-U", "mini_checkers", "--", database)
            self.assertEqual(sql(query, database, node), expected)
            self.assertEqual(sql(f"SELECT count(*) FROM game_players WHERE game_id = '{game}'", database, node), "2")
            self.assertEqual(sql(f"SELECT move_no, from_cell, to_cell FROM moves WHERE game_id = '{game}'", database, node), "1|40|33")
            elapsed = time.monotonic() - start
            self.assertLessEqual(elapsed, 300)
            print(f"Restore verified: game, two players, move; {elapsed:.2f}s <= 300s")
            for target in (database, "mini_checkers"):
                with self.subTest(target=target):
                    self.assertNotEqual(run(restore, archive, target, check=False).returncode, 0)
            self.assertEqual(Path(archive).stat().st_mode & 0o777, 0o600)
            for name, days in (("old", 8), ("recent", 1)):
                path = Path(directory) / f"mini-checkers-{name}.dump"
                path.touch()
                stamp = time.time() - days * 86400
                os.utime(path, (stamp, stamp))
            run(backup, env={"BACKUP_DIR": directory, "BACKUP_RETENTION_DAYS": "7"}, timeout=300)
            self.assertFalse((Path(directory) / "mini-checkers-old.dump").exists())
            self.assertTrue((Path(directory) / "mini-checkers-recent.dump").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
