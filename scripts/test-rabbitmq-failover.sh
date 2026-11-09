#!/usr/bin/env bash
# Local demo only: crash the actual queue leader, verify confirmed backlog,
# then crash the worker's connection node to verify client reconnection.
# No queue purge, node reset, volume removal or API/worker recreation.
set -euo pipefail
cd "$(dirname "$0")/.."
primary="$(scripts/postgres-primary.sh)"
nodes=(rabbitmq rabbitmq-2 rabbitmq-3)
stopped=""
worker_stopped=false
cleanup() {
  if [[ -n "$stopped" ]]; then
    docker start "$(docker compose ps -aq "$stopped")" >/dev/null 2>&1 || true
    docker update --restart=unless-stopped "$(docker compose ps -aq "$stopped")" >/dev/null 2>&1 || true
  fi
  if [[ "$worker_stopped" == true ]]; then docker start "$(docker compose ps -aq event-worker)" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
sql() { docker compose exec -T "$primary" psql -X -v ON_ERROR_STOP=1 -U mini_checkers -d mini_checkers -tAc "$1"; }
api() { docker compose exec -T game-api curl -fsS --max-time 8 "$@"; }
wait_for() {
  local deadline=$((SECONDS + $1)); shift
  until "$@" >/dev/null 2>&1; do
    ((SECONDS < deadline)) || return 1
    sleep 1
  done
}
queues() {
  docker compose exec -T "$control" rabbitmqctl -q list_queues --online --timeout 5 \
    name type leader members online messages --formatter json |
    jq -ce '.[] | select(.name == "game.events.quorum")'
}
all_replicas() { queues | jq -e '.type == "quorum" and (.members | length) == 3 and (.online | length) == 3' >/dev/null; }
cluster_ready() {
  docker compose exec -T "$control" rabbitmqctl -q cluster_status --formatter json |
    jq -e '(.running_nodes | length) == 3 and (.partitions | length) == 0' >/dev/null
}
replicas_caught_up() {
  docker compose exec -T "$control" rabbitmq-queues -q quorum_status \
    --vhost / game.events.quorum --formatter json |
    jq -e 'length == 3 and all(.[]; .["Membership"] == "voter" and (.["Raft State"] == "leader" or .["Raft State"] == "follower")) and ([.[] | .["Commit Index"]] | unique | length) == 1'
}
empty_outbox() { [[ "$(sql "SELECT count(*) FROM outbox_events WHERE game_id = '$1'")" == 0 ]]; }
queued_at_least() { [[ "$(queues | jq -r .messages)" -ge "$1" ]]; }
create_event() {
  game="$(api -X POST http://127.0.0.1:8000/games | jq -er .game_id)"
}
event_for_game() {
  docker compose logs --no-log-prefix event-worker |
    grep -F '"msg":"event processed"' | grep -F "$1" >/dev/null
}
snapshot() {
  docker inspect -f '{{.Id}} {{.State.StartedAt}} {{.RestartCount}}' \
    "$(docker compose ps -q game-api)" "$(docker compose ps -q event-worker)"
}
crash() {
  stopped="$1"
  local id
  id="$(docker compose ps -q "$stopped")"
  docker update --restart=no "$id" >/dev/null
  docker compose kill -s SIGKILL "$stopped" >/dev/null
  [[ "$(docker inspect -f '{{.State.Running}} {{.State.ExitCode}}' "$id")" == 'false 137' ]]
  for node in "${nodes[@]}"; do
    if [[ "$node" != "$stopped" ]]; then control="$node"; break; fi
  done
  echo "PASS $stopped crashed (exit 137) and remains stopped"
}
rejoin() {
  docker start "$(docker compose ps -aq "$stopped")" >/dev/null
  wait_for 60 cluster_ready
  wait_for 30 all_replicas
  wait_for 30 replicas_caught_up
  docker update --restart=unless-stopped "$(docker compose ps -q "$stopped")" >/dev/null
  echo "PASS $stopped rejoined; three online replicas, no reset or volume removal"
  stopped=""
}

control=rabbitmq
wait_for 30 cluster_ready
wait_for 30 all_replicas
echo 'PASS three-node cluster and three-member quorum queue'
leader="$(queues | jq -er .leader)"
old="${leader#rabbit@}"
[[ " ${nodes[*]} " == *" $old "* ]]
api_before="$(docker inspect -f '{{.Id}} {{.State.StartedAt}} {{.RestartCount}}' "$(docker compose ps -q game-api)")"
worker_stopped=true
docker compose stop event-worker >/dev/null
create_event
before_game="$game"
wait_for 15 empty_outbox "$before_game"
wait_for 15 queued_at_least 1
echo "PASS event for $before_game confirmed into the queue before leader failure"
start=$SECONDS
crash "$old"
new_leader() { queues | jq -e --arg old "$leader" '.leader != $old and (.online | length) == 2' >/dev/null; }
wait_for 30 new_leader
echo "PASS leader changed: $leader -> $(queues | jq -r .leader)"
wait_for 15 queued_at_least 1
create_event
after_game="$game"
wait_for 30 empty_outbox "$after_game"
wait_for 15 queued_at_least 2
# docker compose start also follows dependencies and would revive the failed
# broker. Start only this existing worker container, not its dependencies.
docker start "$(docker compose ps -aq event-worker)" >/dev/null
worker_stopped=false
wait_for 30 event_for_game "$before_game"
wait_for 30 event_for_game "$after_game"
[[ "$(docker inspect -f '{{.State.Running}}' "$(docker compose ps -aq "$old")")" == false ]]
[[ "$(docker inspect -f '{{.Id}} {{.State.StartedAt}} {{.RestartCount}}' "$(docker compose ps -q game-api)")" == "$api_before" ]]
duration=$((SECONDS - start))
((duration <= 60))
echo "PASS confirmed backlog and new event processed with old leader still down in ${duration}s <=60s; API not restarted"
rejoin

# Locate the live consumer connection, not an arbitrary replica. This part
# leaves both clients running throughout the failure and recovery.
connections="$(docker compose exec -T "$control" rabbitmqctl -q list_connections name --formatter json)"
consumer_ip="$(jq -er '.[] | .name | select(startswith("10.10.3.4:")) | capture(" -> (?<host>[^:]+):").host' <<<"$connections")"
case "$consumer_ip" in
  10.10.3.3) consumer=rabbitmq ;;
  10.10.3.5) consumer=rabbitmq-2 ;;
  10.10.3.6) consumer=rabbitmq-3 ;;
  *) echo 'Cannot identify the worker connection node' >&2; exit 1 ;;
esac
clients_before="$(snapshot)"
start=$SECONDS
crash "$consumer"
create_event
wait_for 60 event_for_game "$game"
wait_for 15 empty_outbox "$game"
[[ "$(snapshot)" == "$clients_before" ]]
[[ "$(docker inspect -f '{{.State.Running}}' "$(docker compose ps -aq "$consumer")")" == false ]]
duration=$((SECONDS - start))
((duration <= 60))
echo "PASS consumer reconnected and processed new event in ${duration}s <=60s; API and worker not restarted"
rejoin
echo 'PASS RabbitMQ failover complete; all three broker nodes restored'
