#!/usr/bin/env bash
# Assignment 3 check: database, cache and broker flows of the running stack.
# Run from any directory after `docker compose up`. Requires jq on the host.
# HTTP requests are sent from inside the game-api container, so the check
# does not depend on host access to the published port.
# The script stops and starts event-worker and redis on purpose.
set -uo pipefail

cd "$(dirname "$0")/.."
primary="$(scripts/postgres-primary.sh)"

failures=0
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

pass() { echo "PASS  $1"; }
fail() {
  echo "FAIL  $1"
  failures=$((failures + 1))
}

expect_eq() {
  local name="$1" expected="$2" actual="$3"
  if [[ "$actual" == "$expected" ]]; then
    pass "$name"
  else
    fail "$name (expected '$expected', got '$actual')"
  fi
}

# request METHOD PATH [TOKEN] [BODY] -> sets STATUS, CACHE, BODY
request() {
  local args=(-sS -X "$1" -D - "http://127.0.0.1:8000$2")
  [[ -n "${3:-}" ]] && args+=(-H "X-Player-Token: $3")
  [[ -n "${4:-}" ]] && args+=(-H "Content-Type: application/json" --data "$4")
  local raw
  raw="$(docker compose exec -T game-api curl "${args[@]}" | tr -d '\r')"
  STATUS="$(head -n1 <<<"$raw" | awk '{print $2}')"
  CACHE="$(grep -i '^x-cache:' <<<"$raw" | awk '{print $2}')"
  BODY="$(awk 'body {print} /^$/ {body = 1}' <<<"$raw")"
}

sql() {
  docker compose exec -T "$primary" \
    psql -U mini_checkers -d mini_checkers -tAc "$1"
}

redis() { docker compose exec -T redis redis-cli "$@"; }

queue_depth() {
  docker compose exec -T rabbitmq rabbitmqctl -q list_queues name messages |
    awk '$1 == "game.events" {print $2}'
}

worker_events() {
  docker compose logs --no-log-prefix event-worker | grep -F "$1"
}

# wait_until SECONDS COMMAND... -> 0 once COMMAND succeeds
wait_until() {
  local deadline=$((SECONDS + $1))
  shift
  until "$@" >/dev/null 2>&1; do
    ((SECONDS >= deadline)) && return 1
    sleep 1
  done
}

has_event() { worker_events "$1" | grep -q "\"event_type\":\"$2\""; }

outbox_empty() { [[ "$(sql "SELECT count(*) FROM outbox_events WHERE game_id = '$game_id'")" == 0 ]]; }
settle_events() {
  # The outbox also invalidates the cache before sending (including replay).
  # Test HIT/MISS after this finite background work has completed.
  wait_until 15 outbox_empty || fail "outbox drained for $game_id"
}
one_queued_event() { [[ "$(queue_depth)" == 1 ]]; }

echo "--- write: create and join"
request POST /games
expect_eq "POST /games -> 201" 201 "$STATUS"
game_id="$(jq -r .game_id <<<"$BODY")"
white_token="$(jq -r .player_token <<<"$BODY")"
expect_eq "creator plays white" white "$(jq -r .side <<<"$BODY")"
echo "INFO  game_id=$game_id"
settle_events

echo "--- read: cache-aside"
expect_eq "no cache entry before the first read" 0 "$(redis EXISTS "game:$game_id")"
request GET "/games/$game_id"
expect_eq "first GET -> 200" 200 "$STATUS"
expect_eq "first GET is a cache MISS (read from PostgreSQL)" MISS "$CACHE"
expect_eq "new game is waiting" waiting "$(jq -r .status <<<"$BODY")"
expect_eq "cache entry written after the miss" 1 "$(redis EXISTS "game:$game_id")"
ttl="$(redis TTL "game:$game_id")"
if ((ttl > 0 && ttl <= 60)); then pass "cache entry has TTL ($ttl s)"; else fail "cache entry TTL ($ttl)"; fi
request GET "/games/$game_id"
expect_eq "second GET is a cache HIT" HIT "$CACHE"

request POST "/games/$game_id/join"
expect_eq "POST /join -> 200" 200 "$STATUS"
black_token="$(jq -r .player_token <<<"$BODY")"
expect_eq "second player plays black" black "$(jq -r .side <<<"$BODY")"
expect_eq "join invalidated the cache entry" 0 "$(redis EXISTS "game:$game_id")"
request POST "/games/$game_id/join"
expect_eq "third player cannot join -> 409" 409 "$STATUS"
settle_events
request GET "/games/$game_id"
expect_eq "GET after join is a MISS" MISS "$CACHE"
expect_eq "game is active after join" active "$(jq -r .status <<<"$BODY")"

echo "--- write: moves and validation"
move='{"from":[5,0],"to":[4,1]}'
request POST "/games/$game_id/moves" "" "$move"
expect_eq "move without token -> 401" 401 "$STATUS"
request POST "/games/$game_id/moves" "not-a-player" "$move"
expect_eq "move with a foreign token -> 403" 403 "$STATUS"
request POST "/games/$game_id/moves" "$black_token" '{"from":[2,1],"to":[3,0]}'
expect_eq "black cannot move first -> 409" 409 "$STATUS"
request POST "/games/$game_id/moves" "$white_token" '{"from":[5,0],"to":[3,2]}'
expect_eq "illegal move -> 422" 422 "$STATUS"
request POST "/games/$game_id/moves" "$white_token" '{"from":"a1"}'
expect_eq "malformed body -> 400" 400 "$STATUS"
request POST "/games/$game_id/moves" "$white_token" "$move"
expect_eq "white move -> 200" 200 "$STATUS"
expect_eq "turn passes to black" black "$(jq -r .current_turn <<<"$BODY")"
expect_eq "piece moved on the board (row 4)" ".w......" "$(jq -r '.board[4]' <<<"$BODY")"
expect_eq "move invalidated the cache entry" 0 "$(redis EXISTS "game:$game_id")"
settle_events
request GET "/games/$game_id"
expect_eq "GET after move is a MISS with the new state" "MISS 3" "$CACHE $(jq -r .version <<<"$BODY")"

echo "--- database state"
expect_eq "move stored in PostgreSQL" "1|white|40|33" \
  "$(sql "SELECT move_no, player_side, from_cell, to_cell FROM moves WHERE game_id = '$game_id'")"
expect_eq "raw player tokens are not stored" 0 \
  "$(sql "SELECT count(*) FROM game_players WHERE token_hash IN ('$white_token', '$black_token')")"

echo "--- concurrent moves based on the same version"
for target in "[3,0]" "[3,2]"; do
  (
    request POST "/games/$game_id/moves" "$black_token" "{\"from\":[2,1],\"to\":$target}"
    echo "$STATUS" >"$tmp_dir/$target"
  ) &
done
wait
statuses="$(cat "$tmp_dir"/* | sort | tr '\n' ' ')"
expect_eq "exactly one of two concurrent moves succeeds" "200 409 " "$statuses"
expect_eq "PostgreSQL has exactly two moves" 2 \
  "$(sql "SELECT count(*) FROM moves WHERE game_id = '$game_id'")"

echo "--- finish"
request POST "/games/$game_id/finish" "$white_token"
expect_eq "white resigns -> 200" 200 "$STATUS"
expect_eq "game is finished, black wins" "finished black" \
  "$(jq -r '"\(.status) \(.winner)"' <<<"$BODY")"
request POST "/games/$game_id/moves" "$black_token" '{"from":[2,3],"to":[3,4]}'
expect_eq "no moves after finish -> 409" 409 "$STATUS"
expect_eq "final status persisted" "finished|black" \
  "$(sql "SELECT status, winner FROM games WHERE id = '$game_id'")"

echo "--- broker: events consumed by event-worker"
for type in game.created player.joined move.made game.finished; do
  if wait_until 15 has_event "$game_id" "$type"; then
    pass "event-worker processed $type"
  else
    fail "event-worker processed $type"
  fi
done
expect_eq "event-worker processed both moves" 2 \
  "$(worker_events "$game_id" | grep -c '"event_type":"move.made"')"
echo "INFO  $(worker_events "$game_id" | grep '"event_type":"move.made"' | tail -n1)"

echo "--- broker: events wait in the durable queue while the worker is down"
docker compose stop event-worker >/dev/null 2>&1
request POST /games
queued_game="$(jq -r .game_id <<<"$BODY")"
if wait_until 10 one_queued_event; then
  pass "event is queued while event-worker is stopped"
else
  fail "event is queued while event-worker is stopped (depth $(queue_depth))"
fi
docker compose start event-worker >/dev/null 2>&1
if wait_until 30 has_event "$queued_game" game.created; then
  pass "restarted event-worker processed the queued event"
else
  fail "restarted event-worker processed the queued event"
fi
expect_eq "queue is empty after processing" 0 "$(queue_depth)"

echo "--- cache outage: reads fall back to PostgreSQL"
docker compose stop redis >/dev/null 2>&1
request GET "/games/$game_id"
expect_eq "GET without Redis -> 200 BYPASS" "200 BYPASS" "$STATUS $CACHE"
docker compose start redis >/dev/null 2>&1
cache_hit() {
  request GET "/games/$game_id"
  request GET "/games/$game_id"
  [[ "$STATUS $CACHE" == "200 HIT" ]]
}
if wait_until 30 cache_hit; then
  pass "cache works again after Redis restart"
else
  fail "cache works again after Redis restart (last: $STATUS $CACHE)"
fi

if ((failures > 0)); then
  echo "Result: ${failures} check(s) failed"
  exit 1
fi
echo "Result: all checks passed"
