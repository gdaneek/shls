#!/usr/bin/env bash
# Assignment 2 check: the stack from docker-compose.yml is up and every
# component answers. Run from the repository root after `docker compose up`.
set -uo pipefail

cd "$(dirname "$0")/.."

failures=0

check() {
  local name="$1"
  shift
  if "$@" >/dev/null 2>&1; then
    echo "PASS  $name"
  else
    echo "FAIL  $name"
    failures=$((failures + 1))
  fi
}

container_id() {
  docker compose ps -q "$1"
}

is_healthy() {
  local id
  id="$(container_id "$1")"
  [[ -n "$id" ]] &&
    [[ "$(docker inspect -f '{{.State.Health.Status}}' "$id")" == "healthy" ]]
}

not_published() {
  local id
  id="$(container_id "$1")"
  [[ -n "$id" ]] && [[ -z "$(docker port "$id")" ]]
}

hello_in_container() {
  [[ "$(docker compose exec -T game-api curl -fsS http://127.0.0.1:8000/hello)" \
    == '{"message":"hello-world"}' ]]
}

postgres_query() {
  local primary
  primary="$(scripts/postgres-primary.sh)" || return 1
  [[ "$(docker compose exec -T "$primary" \
    psql -U mini_checkers -d mini_checkers -tAc 'SELECT 1')" == "1" ]]
}

redis_ping() {
  [[ "$(docker compose exec -T redis redis-cli ping)" == "PONG" ]]
}

for service in frontend game-api event-worker postgres postgres-standby redis rabbitmq; do
  check "$service container is healthy" is_healthy "$service"
done

check "game-api GET /hello returns hello-world" hello_in_container
check "frontend serves HTML" docker compose exec -T frontend \
  wget -q -O /dev/null http://127.0.0.1:8080/
check "frontend proxies API requests" docker compose exec -T frontend \
  wget -q -O /dev/null http://127.0.0.1:8080/api/hello
check "postgres executes SELECT 1" postgres_query
check "exactly one PostgreSQL node is writable" scripts/postgres-primary.sh
check "redis answers PONG" redis_ping
check "rabbitmq AMQP listener accepts connections" \
  docker compose exec -T rabbitmq rabbitmq-diagnostics -q check_port_connectivity

for service in game-api postgres postgres-standby redis rabbitmq event-worker; do
  check "$service has no ports published on the host" not_published "$service"
done

# Host access is informational: a VPN or local firewall may block it.
api_port="$(docker compose port frontend 8080 2>/dev/null | awk -F: '{print $NF}')"
if [[ -n "$api_port" ]] &&
  host_reply="$(curl -fsS --max-time 3 "http://127.0.0.1:${api_port}/api/hello" 2>/dev/null)"; then
  echo "INFO  host request to 127.0.0.1:${api_port}/api/hello -> ${host_reply}"
else
  echo "INFO  host request to the published API port failed (check VPN/firewall)"
fi

if ((failures > 0)); then
  echo "Result: ${failures} check(s) failed"
  exit 1
fi
echo "Result: all checks passed"
