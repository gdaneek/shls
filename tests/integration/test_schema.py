"""Проверяет создание SQL-схемы, повторное применение и обновление старых таблиц без потери данных.
Работает в отдельной временной базе и сверяет SQL-файл с копией в образе API.
"""

import unittest
import uuid

from support import ROOT, compose, primary, sql


class Schema(unittest.TestCase):
    def setUp(self):
        self.node = primary()
        self.database = "mini_checkers_schema_test_" + uuid.uuid4().hex
        compose("exec", "-T", self.node, "createdb", "-U", "mini_checkers", "-T", "template0", "--", self.database)
        self.addCleanup(compose, "exec", "-T", self.node, "dropdb", "-U", "mini_checkers", "--", self.database)

    def query(self, query):
        return sql(query, database=self.database, node=self.node)

    def apply_schema(self):
        schema = (ROOT / "infra/postgres/schema.sql").read_text()
        compose("exec", "-T", "-e", "PGOPTIONS=-c client_min_messages=warning", self.node,
                "psql", "-X", "-v", "ON_ERROR_STOP=1", "-U", "mini_checkers",
                "-d", self.database, input_text=schema)

    def test_initialization_idempotency_and_legacy_upgrade(self):
        self.apply_schema()
        tables = self.query("SELECT count(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE'")
        self.assertEqual(tables, "4")
        game = "10000000-0000-4000-8000-000000000001"
        self.query(f"""
            INSERT INTO games (id, status, board, current_turn) VALUES
              ('{game}', 'waiting', '.w.b....' || repeat('.', 48) || 'w.b.....', 'white');
            INSERT INTO game_players (game_id, side, token_hash) VALUES ('{game}', 'white', 'schema-test-hash');
            INSERT INTO moves (game_id, move_no, player_side, from_cell, to_cell) VALUES ('{game}', 1, 'white', 40, 33);
            INSERT INTO outbox_events (game_id, type, request_id) VALUES ('{game}', 'game.created', 'schema-test');
        """)
        self.apply_schema()
        counts = "SELECT (SELECT count(*) FROM games), (SELECT count(*) FROM game_players), (SELECT count(*) FROM moves), (SELECT count(*) FROM outbox_events)"
        self.assertEqual(self.query(counts), "1|1|1|1")
        self.assertEqual(self.query("SELECT substr(board, 1, 8), substr(board, 57, 8), version FROM games"), ".W.b....|w.B.....|2")
        self.apply_schema()
        self.assertEqual(self.query(counts), "1|1|1|1")
        self.assertEqual(self.query("SELECT version FROM games"), "2")
        self.query("ALTER TABLE games DROP COLUMN capture_from, DROP COLUMN captured_cells; "
                   "ALTER TABLE moves DROP COLUMN captured_cell, DROP COLUMN promoted;")
        self.apply_schema()
        self.assertEqual(self.query(counts), "1|1|1|1")
        self.assertEqual(self.query("SELECT COALESCE(capture_from, -1), captured_cells, promoted FROM games CROSS JOIN moves"), "-1|[]|f")
        image_schema = compose("exec", "-T", "game-api", "sh", "-c", 'cat "$POSTGRES_SCHEMA_FILE"')
        self.assertEqual(image_schema, (ROOT / "infra/postgres/schema.sql").read_text().strip())


if __name__ == "__main__":
    unittest.main(verbosity=2)
