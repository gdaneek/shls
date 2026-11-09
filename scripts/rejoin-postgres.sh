#!/usr/bin/env bash
# Rewind a STOPPED former primary to the current primary, preserving its old
# physical contents in a host-side archive before making any changes.
set -euo pipefail
cd "$(dirname "$0")/.."
case "${1:-}" in
  postgres|postgres-standby) target="$1" ;;
  *) echo "usage: $0 STOPPED_NODE" >&2; exit 2 ;;
esac
source="$(scripts/postgres-primary.sh)"
[[ "$source" != "$target" ]] || { echo 'Refusing to rewind the current primary' >&2; exit 1; }
target_id="$(docker compose ps -aq "$target")"
[[ -n "$target_id" ]] &&
  [[ "$(docker inspect -f '{{.State.Running}} {{.State.Restarting}}' "$target_id")" == 'false false' ]] || {
    echo 'Target must be stopped' >&2; exit 1
  }
docker update --restart=no "$target_id" >/dev/null
docker compose --profile tools run --rm -T --no-deps postgres-maintenance fence "$target" >/dev/null
umask 077
mkdir -p backups
partial="$(mktemp "$PWD/backups/postgres-before-rejoin-${target}-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX.tar.gz.partial")"
trap 'rm -f "$partial"' EXIT
docker compose --profile tools run --rm -T --no-deps postgres-maintenance archive "$target" >"$partial"
tar -tzf "$partial" >/dev/null
archive="${partial%.partial}"
mv "$partial" "$archive"
echo "Old node archived: $archive"
docker compose --profile tools run --rm -T --no-deps postgres-maintenance rewind "$target" "$source"
lsn="$(docker compose exec -T "$source" psql -X -U mini_checkers -d mini_checkers -tAc 'SELECT pg_current_wal_flush_lsn()')"
docker compose start "$target" >/dev/null
deadline=$((SECONDS + 60))
until [[ "$(docker compose exec -T "$target" psql -X -U mini_checkers -d mini_checkers \
  -tAc "SELECT pg_is_in_recovery() AND pg_last_wal_replay_lsn() >= '$lsn'::pg_lsn" 2>/dev/null)" == t ]]; do
  ((SECONDS < deadline)) || { echo "Standby did not catch up; inspect $target logs" >&2; exit 1; }
  sleep 1
done
docker update --restart=unless-stopped "$target_id" >/dev/null
echo "Rejoined $target as standby of $source; replication caught up."
