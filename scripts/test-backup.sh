#!/usr/bin/env bash
# Real restore into a NEW temporary database, compare records, then remove it.
set -euo pipefail
cd "$(dirname "$0")/.."
database=""
policy_directory=""
cleanup() {
  if [[ -n "$database" ]]; then
    docker compose exec -T postgres dropdb -U mini_checkers -- "$database"
  fi
  if [[ -n "$policy_directory" ]]; then
    rm -f "$policy_directory"/mini-checkers-* "$policy_directory/.backup.lock"
    rmdir "$policy_directory"
  fi
}
trap cleanup EXIT
sql() { docker compose exec -T postgres psql -X -v ON_ERROR_STOP=1 -U mini_checkers -d "$1" -tAc "$2"; }

# Stable fixture. Other games may change while pg_dump takes its snapshot.
created="$(docker compose exec -T game-api curl -fsS -X POST http://127.0.0.1:8000/games)"
game="$(jq -er '.game_id' <<<"$created")"
token="$(jq -er '.player_token' <<<"$created")"
docker compose exec -T game-api curl -fsS -X POST "http://127.0.0.1:8000/games/$game/join" >/dev/null
docker compose exec -T game-api curl -fsS -X POST "http://127.0.0.1:8000/games/$game/moves" \
  -H "X-Player-Token: $token" -H 'Content-Type: application/json' \
  -d '{"from":[5,0],"to":[4,1]}' >/dev/null
query="SELECT status, board, current_turn, version FROM games WHERE id = '$game';"
expected="$(sql mini_checkers "$query")"
start=$SECONDS
archive="$(scripts/backup.sh)"
database="$(scripts/restore.sh "$archive")"
[[ "$(sql "$database" "$query")" == "$expected" ]]
[[ "$(sql "$database" "SELECT count(*) FROM game_players WHERE game_id = '$game'")" == 2 ]]
[[ "$(sql "$database" "SELECT move_no || '|' || from_cell || '|' || to_cell FROM moves WHERE game_id = '$game'")" == '1|40|33' ]]
duration=$((SECONDS - start))
((duration <= 300))
echo "PASS restore: game $game, 2 players, 1 move; ${duration}s <= 300s; $archive"

# Safety guards: refuse an existing target and the application's live DB.
if scripts/restore.sh "$archive" "$database" >/dev/null 2>&1; then
  echo 'FAIL: restore overwrote an existing database' >&2; exit 1
fi
if scripts/restore.sh "$archive" mini_checkers >/dev/null 2>&1; then
  echo 'FAIL: restore accepted the live database' >&2; exit 1
fi
[[ "$(stat -c %a "$archive")" == 600 ]]
echo 'PASS restore safety checks and archive permissions (600)'

# Retention is applied only to completed archives of this project.
policy_directory="$(mktemp -d "$PWD/backups/policy-test-XXXXXX")"
touch -d '8 days ago' "$policy_directory/mini-checkers-old.dump"
touch -d '1 day ago' "$policy_directory/mini-checkers-recent.dump"
BACKUP_DIR="$policy_directory" scripts/backup.sh >/dev/null
[[ ! -e "$policy_directory/mini-checkers-old.dump" ]]
[[ -e "$policy_directory/mini-checkers-recent.dump" ]]
echo 'PASS retention: old archive removed, recent archive retained'
