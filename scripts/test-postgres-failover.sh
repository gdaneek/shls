#!/usr/bin/env bash
# Local demo: SIGKILL primary, explicit promotion, API writes on the new
# primary, then pg_rewind rejoin. Roles may swap; no volumes are deleted.
set -euo pipefail
cd "$(dirname "$0")/.."
completed=false
sentinel=""
cleanup() {
  if [[ -n "$sentinel" ]]; then docker compose exec -T redis redis-cli DEL "$sentinel" >/dev/null || true; fi
  if [[ "$completed" == false ]]; then echo 'Inspect PostgreSQL roles before recovery; do NOT blindly restart the old primary.' >&2; fi
}
trap cleanup EXIT
sql() {
  local node="$1"; shift
  docker compose exec -T "$node" psql -X -v ON_ERROR_STOP=1 -U mini_checkers -d mini_checkers -tAc "$1"
}
api() { docker compose exec -T game-api curl -sS --max-time 8 "$@"; }
wait_for() {
  local deadline=$((SECONDS + $1)); shift
  until "$@" >/dev/null 2>&1; do
    ((SECONDS < deadline)) || return 1
    sleep 1
  done
}
old="$(scripts/postgres-primary.sh)"
if [[ "$old" == postgres ]]; then candidate=postgres-standby; else candidate=postgres; fi
[[ "$(sql "$candidate" 'SELECT pg_is_in_recovery()')" == t ]]

created="$(api -f -X POST http://127.0.0.1:8000/games)"
game="$(jq -er .game_id <<<"$created")"
white="$(jq -er .player_token <<<"$created")"
joined="$(api -f -X POST "http://127.0.0.1:8000/games/$game/join")"
black="$(jq -er .player_token <<<"$joined")"
api -f -X POST "http://127.0.0.1:8000/games/$game/moves" \
  -H "X-Player-Token: $white" -H 'Content-Type: application/json' \
  -d '{"from":[5,0],"to":[4,1]}' >/dev/null
lsn="$(sql "$old" 'SELECT pg_current_wal_flush_lsn()')"
replicated() {
  [[ "$(sql "$candidate" "SELECT pg_last_wal_replay_lsn() >= '$lsn'::pg_lsn AND EXISTS (SELECT 1 FROM games WHERE id = '$game' AND version = 3)")" == t ]]
}
wait_for 30 replicated
sentinel="test:postgres-failover:$game"
docker compose exec -T redis redis-cli SET "$sentinel" preserve >/dev/null
api -f "http://127.0.0.1:8000/games/$game" >/dev/null
echo "PASS streaming replica replayed game $game and its first move"
scripts/backup.sh >/dev/null

# These commands deliberately suppress Docker auto-restart; recovery must
# come from the standby, not from reviving the failed primary.
old_id="$(docker compose ps -q "$old")"
docker update --restart=no "$old_id" >/dev/null
start=$SECONDS
docker compose kill -s SIGKILL "$old" >/dev/null
[[ "$(docker inspect -f '{{.State.Running}}' "$old_id")" == false ]]
[[ "$(docker inspect -f '{{.State.ExitCode}}' "$old_id")" == 137 ]]
echo "PASS $old crashed (exit 137), not restarted"
scripts/promote-postgres.sh "$candidate"
ready() { [[ "$(api -o /dev/null -w '%{http_code}' http://127.0.0.1:8000/health/ready)" == 200 ]]; }
wait_for 60 ready
[[ "$(scripts/postgres-primary.sh)" == "$candidate" ]]
[[ "$(docker compose exec -T redis redis-cli GET "$sentinel")" == preserve ]]
[[ "$(docker compose exec -T redis redis-cli EXISTS "game:$game")" == 0 ]]
echo 'PASS stale game cache cleared, unrelated Redis key preserved'
docker compose exec -T redis redis-cli DEL "game:$game" >/dev/null
[[ "$(api -f "http://127.0.0.1:8000/games/$game" | jq -r .version)" == 3 ]]
state="$(api -f -X POST "http://127.0.0.1:8000/games/$game/moves" \
  -H "X-Player-Token: $black" -H 'Content-Type: application/json' \
  -d '{"from":[2,1],"to":[3,0]}')"
[[ "$(jq -r .version <<<"$state")" == 4 ]]
[[ "$(sql "$candidate" "SELECT count(*) FROM moves WHERE game_id = '$game'")" == 2 ]]
duration=$((SECONDS - start))
((duration <= 60))
echo "PASS API redirected/recreated and read/wrote the new primary in ${duration}s <=60s"

scripts/rejoin-postgres.sh "$old"
[[ "$(sql "$old" 'SELECT pg_is_in_recovery()')" == t ]]
[[ "$(sql "$old" "SELECT version FROM games WHERE id = '$game'")" == 4 ]]
[[ "$(scripts/postgres-primary.sh)" == "$candidate" ]]
[[ "$(docker inspect -f '{{.State.Running}}' "$old_id")" == true ]]
echo "PASS old primary rejoined as streaming standby; both moves preserved"
scripts/backup.sh >/dev/null
echo "PASS backup works after role switch; current primary=$candidate, standby=$old"
completed=true
