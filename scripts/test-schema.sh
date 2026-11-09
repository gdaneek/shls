#!/usr/bin/env bash
# Verify the canonical SQL file in a NEW temporary database, never the live DB.
set -euo pipefail
cd "$(dirname "$0")/.."
primary="$(scripts/postgres-primary.sh)"
database="mini_checkers_schema_test_$(date -u +%Y%m%d_%H%M%S)_$$"
created=false
cleanup() {
  if [[ "$created" == true ]]; then
    docker compose exec -T "$primary" dropdb -U mini_checkers -- "$database"
  fi
}
trap cleanup EXIT

sql() {
  docker compose exec -T "$primary" psql -X -v ON_ERROR_STOP=1 \
    -U mini_checkers -d "$database" -tAc "$1"
}
apply_schema() {
  docker compose exec -T -e 'PGOPTIONS=-c client_min_messages=warning' "$primary" \
    psql -X -v ON_ERROR_STOP=1 -U mini_checkers -d "$database" \
    < infrastructure/postgres/schema.sql >/dev/null
}
check_rows() {
  [[ "$(sql 'SELECT (SELECT count(*) FROM games), (SELECT count(*) FROM game_players),
    (SELECT count(*) FROM moves), (SELECT count(*) FROM outbox_events)')" == '1|1|1|1' ]]
}

docker compose exec -T "$primary" createdb -U mini_checkers -T template0 -- "$database"
created=true
apply_schema
[[ "$(sql "SELECT count(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE'")" == 4 ]]
echo 'PASS SQL file initializes an empty database with four tables'

# Synthetic rows belong only to this disposable test database. Place old-rule
# uncrowned men on their last rank, alongside opposing men that must NOT crown.
sql "INSERT INTO games (id, status, board, current_turn) VALUES
 ('10000000-0000-4000-8000-000000000001', 'waiting', '.w.b....' || repeat('.', 48) || 'w.b.....', 'white');
 INSERT INTO game_players (game_id, side, token_hash) VALUES
 ('10000000-0000-4000-8000-000000000001', 'white', 'schema-test-hash');
 INSERT INTO moves (game_id, move_no, player_side, from_cell, to_cell) VALUES
 ('10000000-0000-4000-8000-000000000001', 1, 'white', 40, 33);
 INSERT INTO outbox_events (game_id, type, request_id) VALUES
 ('10000000-0000-4000-8000-000000000001', 'game.created', 'schema-test');" >/dev/null
apply_schema
check_rows
[[ "$(sql 'SELECT substr(board, 1, 8), substr(board, 57, 8), version FROM games')" == '.W.b....|w.B.....|2' ]]
echo 'PASS existing records survive; legacy men crown only on their promotion rank'

apply_schema
check_rows
[[ "$(sql 'SELECT version FROM games')" == 2 ]]
echo 'PASS reapplying the schema is idempotent'

# Simulate the previous schema only inside our disposable database.
sql 'ALTER TABLE games DROP COLUMN capture_from, DROP COLUMN captured_cells;
     ALTER TABLE moves DROP COLUMN captured_cell, DROP COLUMN promoted;' >/dev/null
apply_schema
check_rows
[[ "$(sql 'SELECT COALESCE(capture_from, -1), captured_cells, promoted FROM games CROSS JOIN moves')" == '-1|[]|f' ]]
echo 'PASS legacy tables gain capture columns without losing games, players, moves or events'

cmp -s infrastructure/postgres/schema.sql \
  <(docker compose exec -T game-api sh -c 'cat "$POSTGRES_SCHEMA_FILE"')
echo 'PASS the running API image contains the same SQL file'
