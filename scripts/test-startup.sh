#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
docker compose config --quiet
docker compose exec -T postgres pg_isready -U mini_checkers -d mini_checkers
docker compose exec -T redis redis-cli ping | grep -qx PONG
docker compose exec -T rabbitmq rabbitmq-diagnostics -q check_running

# Check the HTTP service inside its container, independently of the host VPN.
docker compose exec -T game-api curl --fail --silent --max-time 5 \
  http://127.0.0.1:8000/hello | grep -q '"hello-world"'
echo "PASS: API, PostgreSQL, Redis, RabbitMQ"
