#!/usr/bin/env bash
# One-time upgrade of the existing single-node demo. Never deletes a queue,
# resets a RabbitMQ node, or removes volumes. Run without concurrent writes.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -f .env ]] || { echo 'Create .env first' >&2; exit 1; }
chmod 600 .env
# Compose needs the new variable even for exec against the old container.
# Reuse that node's secret; changing it would break existing CLI/peer auth.
if ! grep -q '^RABBITMQ_ERLANG_COOKIE=' .env; then
  cookie="$(RABBITMQ_ERLANG_COOKIE=temporaryconfigurationvalue docker compose exec -T \
    rabbitmq cat /var/lib/rabbitmq/.erlang.cookie)"
  [[ "$cookie" =~ ^[A-Za-z0-9]{20,64}$ ]]
  umask 077
  printf '\nRABBITMQ_ERLANG_COOKIE=%s\n' "$cookie" >> .env
  unset cookie
  echo 'Existing RabbitMQ cookie preserved in ignored .env (value not displayed).'
fi
docker compose config -q
docker compose stop game-api >/dev/null
complete=false
trap 'if [[ "$complete" == false ]]; then echo "Upgrade interrupted. Volumes and queues retained; inspect logs before continuing." >&2; fi' EXIT
deadline=$((SECONDS + 60))
empty_samples=0
while true; do
  queues="$(docker compose exec -T rabbitmq rabbitmqctl -q list_queues name messages --formatter json)"
  remaining="$(jq -r '[.[] | select(.name == "game.events") | .messages] | add // 0' <<<"$queues")"
  if [[ "$remaining" == 0 ]]; then
    empty_samples=$((empty_samples + 1))
    ((empty_samples >= 2)) && break
    sleep 5  # Quorum queue metrics can lag behind the last delivery/ACK.
    continue
  fi
  empty_samples=0
  [[ "$(docker compose exec -T event-worker printenv EVENTS_QUEUE)" == game.events ]] || {
    echo 'Old queue is nonempty: drain it with the previous worker before upgrading' >&2; exit 1;
  }
  ((SECONDS < deadline)) || { echo 'Old queue did not drain; nothing was deleted' >&2; exit 1; }
  sleep 1
done
docker compose stop event-worker >/dev/null
echo 'Old game.events queue drained and retained; pending PostgreSQL outbox rows will use the new quorum queue.'
infra/firewall/firewall.sh apply
docker compose up --build -d --wait --wait-timeout 240
complete=true
echo 'Upgrade complete. Verify with scripts/test-rabbitmq-failover.sh.'
