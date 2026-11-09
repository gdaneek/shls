#!/usr/bin/env bash
# Controlled failures; use only on the student's local/demo stack.
# HTTP probes run inside the API container to avoid VPN/host-port dependencies.
# Recovery time includes Docker's healthcheck interval, not just HTTP downtime.
set -euo pipefail
cd "$(dirname "$0")/.."
primary="$(scripts/postgres-primary.sh)"
failures=0
restart_services=()
cleanup() {
  if ((${#restart_services[@]})); then
    docker compose start "${restart_services[@]}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT
check() {
  if "$@"; then echo "PASS $*"; else echo "FAIL $*"; failures=$((failures + 1)); fi
}
wait_for() {
  local limit="$1"; shift
  local start=$SECONDS
  until "$@" >/dev/null 2>&1; do
    ((SECONDS - start < limit)) || return 1
    sleep 1
  done
}
sql() { docker compose exec -T "$primary" psql -X -v ON_ERROR_STOP=1 -U mini_checkers -d mini_checkers -tAc "$1"; }
api() { docker compose exec -T game-api curl -sS --max-time 8 "$@"; }
request_status() { api -o /dev/null -w '%{http_code}' "http://127.0.0.1:8000/$1"; }
id() { docker compose ps -aq "$1"; }
healthy() { [[ "$(docker inspect -f '{{.State.Health.Status}}' "$(id "$1")")" == healthy ]]; }
restarts() { docker inspect -f '{{.RestartCount}}' "$(id "$1")"; }
recovered() {
  # One snapshot: don't combine pre-exit health with a post-exit restart count.
  local count running status
  read -r count running status <<<"$(docker inspect -f \
    '{{.RestartCount}} {{.State.Running}} {{.State.Health.Status}}' "$(id "$1")")"
  ((count > $2)) && [[ "$running $status" == 'true healthy' ]]
}
processed() { docker compose logs --no-log-prefix event-worker | grep -F "$1" >/dev/null; }
outbox_empty() { [[ "$(sql "SELECT count(*) FROM outbox_events WHERE game_id = '$1'")" == 0 ]]; }
cache_recovered() {
  api "http://127.0.0.1:8000/games/$game" >/dev/null
  api -D - "http://127.0.0.1:8000/games/$game" | grep -qi 'x-cache: HIT'
}

healthy game-api && healthy event-worker && healthy postgres && healthy postgres-standby && healthy redis && healthy rabbitmq
created="$(api -f -X POST http://127.0.0.1:8000/games)"
game="$(jq -er '.game_id' <<<"$created")"
token="$(jq -er '.player_token' <<<"$created")"
wait_for 15 processed "$game"
wait_for 15 outbox_empty "$game"

# These are process exits, NOT `docker compose stop` / `docker kill`:
# Docker treats the latter as administrative stops, suppressing restart.
restart_test() {
  local service="$1" limit="$2" count start remaining trigger_status=0
  count="$(restarts "$service")"; start=$SECONDS
  case "$service" in
    game-api|event-worker) docker compose exec -T "$service" sh -c 'kill -TERM 1' ;;
    postgres|postgres-standby) docker compose exec -T --user postgres "$service" sh -c 'kill -QUIT 1' ;;
    redis) docker compose exec -T redis redis-cli shutdown nosave ;;
    rabbitmq) docker compose exec -T rabbitmq rabbitmqctl stop >/dev/null ;;
  esac || trigger_status=$?
  # The container may exit before docker exec receives its command's status
  # (137/143). Recovery is verified below by RestartCount AND health, not by
  # the exit status of the deliberately interrupted control command.
  if ((trigger_status)); then
    echo "INFO $service control exec exited $trigger_status during process shutdown"
  fi
  remaining=$((limit - (SECONDS - start)))
  if ((remaining >= 0)) && wait_for "$remaining" recovered "$service" "$count"; then
    echo "PASS auto-restart $service: $((SECONDS - start))s <= ${limit}s; restart count $(restarts "$service")"
  else
    echo "FAIL auto-restart $service after ${limit}s"; failures=$((failures + 1))
  fi
}

restart_test game-api 30
check test "$(api -f "http://127.0.0.1:8000/games/$game" | jq -r .game_id)" = "$game"
restart_test event-worker 30
restart_test redis 30
restart_test "$primary" 60
check test "$(request_status health/ready)" = 200
check test "$(sql "SELECT status FROM games WHERE id = '$game'")" = waiting
restart_test rabbitmq 60
check wait_for 30 healthy event-worker

echo '--- Worker stopped: publish, restart broker, then consume persistent message'
restart_services+=(event-worker)
docker compose stop event-worker >/dev/null
queued="$(api -f -X POST http://127.0.0.1:8000/games | jq -er .game_id)"
check wait_for 15 outbox_empty "$queued"
restart_test rabbitmq 60
docker compose start event-worker >/dev/null
check wait_for 30 processed "$queued"

echo '--- Redis outage: DB fallback and writes still work'
restart_services+=(redis)
docker compose stop redis >/dev/null
check test "$(api -D - "http://127.0.0.1:8000/games/$game" | tr -d '\r' | awk 'tolower($1)=="x-cache:" {print $2}')" = BYPASS
check test "$(request_status health/ready)" = 200
check test "$(api -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:8000/games/$game/finish" -H "X-Player-Token: $token")" = 200
docker compose start redis >/dev/null
check wait_for 30 cache_recovered

echo '--- PostgreSQL outage: live=200, ready=503, writes=503; recover connections'
restart_services+=("$primary")
docker compose stop "$primary" >/dev/null
check test "$(request_status health/live)" = 200
check test "$(request_status health/ready)" = 503
check test "$(api -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8000/games)" = 503
docker compose start "$primary" >/dev/null
check wait_for 60 healthy "$primary"
ready() { [[ "$(request_status health/ready)" == 200 ]]; }
check wait_for 30 ready
check test "$(sql "SELECT status FROM games WHERE id = '$game'")" = finished

echo '--- Broker outage + API process exit: committed outbox survives'
restart_services+=(rabbitmq)
docker compose stop rabbitmq >/dev/null
pending="$(api -f -X POST http://127.0.0.1:8000/games | jq -er .game_id)"
check test "$(sql "SELECT count(*) FROM outbox_events WHERE game_id = '$pending'")" = 1
check test "$(request_status health/ready)" = 200
restart_test game-api 30
check test "$(sql "SELECT count(*) FROM outbox_events WHERE game_id = '$pending'")" = 1
docker compose start rabbitmq >/dev/null
check wait_for 60 processed "$pending"
check wait_for 15 outbox_empty "$pending"
check wait_for 30 healthy event-worker

echo "Result: $failures failed checks"
((failures == 0))
