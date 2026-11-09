#!/usr/bin/env bash
# Explicit/manual failover. Never promote while the other node can run writes.
set -euo pipefail
cd "$(dirname "$0")/.."
case "${1:-}" in
  postgres) candidate=postgres; old=postgres-standby ;;
  postgres-standby) candidate=postgres-standby; old=postgres ;;
  *) echo "usage: $0 postgres|postgres-standby" >&2; exit 2 ;;
esac
old_id="$(docker compose ps -aq "$old")"
[[ -n "$old_id" ]] &&
  [[ "$(docker inspect -f '{{.State.Running}} {{.State.Restarting}}' "$old_id")" == 'false false' ]] || {
    echo "Refusing promotion: $old must be stopped, not running or restarting" >&2
    exit 1
  }
docker update --restart=no "$old_id" >/dev/null
docker compose --profile tools run --rm -T --no-deps postgres-maintenance fence "$old" >/dev/null
role="$(docker compose exec -T "$candidate" psql -X -U mini_checkers -d mini_checkers -tAc 'SELECT pg_is_in_recovery()')"
if [[ "$role" == t ]]; then
  docker compose exec -T --user postgres "$candidate" sh -c 'exec pg_ctl -D "$PGDATA" promote -w -t 30'
elif [[ "$role" != f ]]; then
  echo 'Cannot determine the candidate role' >&2; exit 1
fi
# The installed Drogon/libpq event-loop adapter does not reliably recover a
# multi-host connection. Keep the student setup simple: explicitly redirect
# and recreate the stateless API rather than patch the driver or add a proxy.
[[ -f .env ]] || { echo '.env is required to persist the selected primary' >&2; exit 1; }
if grep -q '^POSTGRES_PRIMARY_HOST=' .env; then
  sed -i "s/^POSTGRES_PRIMARY_HOST=.*/POSTGRES_PRIMARY_HOST=$candidate/" .env
else
  printf '\nPOSTGRES_PRIMARY_HOST=%s\n' "$candidate" >> .env
fi
POSTGRES_PRIMARY_HOST="$candidate" docker compose up --no-deps -d --force-recreate \
  --wait --wait-timeout 60 game-api >/dev/null
# Old cached states may be ahead of an asynchronously replicated standby.
# Clear only disposable game keys AFTER the old API is gone, not FLUSHALL.
if docker compose exec -T redis bash -o pipefail -c \
  'redis-cli --scan --pattern "game:*" | while IFS= read -r key; do redis-cli UNLINK "$key" >/dev/null || exit 1; done'; then
  echo 'Game cache invalidated after promotion (unrelated Redis keys retained).'
else
  echo 'WARNING: Redis unavailable; ensure the disposable cache is empty when it returns.' >&2
fi
echo "Primary: $candidate; $old is stopped and fenced. Rejoin it before normal compose up."
