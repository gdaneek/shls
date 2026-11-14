#!/usr/bin/env bash
# Restore into a NEW test database only; never overwrite the live database.
# Usage: infra/backups/restore.sh ARCHIVE [mini_checkers_restore_NAME]
# stdout: restored database name.
set -euo pipefail
[[ $# -ge 1 && $# -le 2 ]] || { echo "usage: $0 ARCHIVE [TEST_DB]" >&2; exit 2; }
archive="$(realpath "$1")"
database="${2:-mini_checkers_restore_$(date -u +%Y%m%d_%H%M%S)_$$}"
[[ "$database" =~ ^mini_checkers_restore_[a-z0-9_]+$ && ${#database} -le 63 ]] || {
  echo 'Target must start with mini_checkers_restore_ and be <=63 characters' >&2
  exit 2
}
[[ -s "$archive" ]] || { echo 'Archive is empty or missing' >&2; exit 2; }
cd "$(dirname "$0")/../.."
primary="$(infra/postgres/postgres-primary.sh)"
docker compose exec -T "$primary" pg_restore --list <"$archive" >/dev/null
# createdb fails if this name already exists. No --clean / --create restore.
docker compose exec -T "$primary" sh -c \
  'exec createdb -U "$POSTGRES_USER" -T template0 -- "$1"' sh "$database"
complete=false
cleanup() {
  if [[ "$complete" == false ]]; then
    docker compose exec -T "$primary" sh -c \
      'exec dropdb -U "$POSTGRES_USER" -- "$1"' sh "$database" || true
  fi
}
trap cleanup EXIT
timeout 240 docker compose exec -T "$primary" sh -c \
  'exec pg_restore -U "$POSTGRES_USER" -d "$1" --single-transaction --exit-on-error --no-owner --no-acl' \
  sh "$database" <"$archive"
docker compose exec -T "$primary" sh -c \
  'exec psql -X -v ON_ERROR_STOP=1 -U "$POSTGRES_USER" -d "$1" -c "SELECT
    (SELECT count(*) FROM games) AS games,
    (SELECT count(*) FROM game_players) AS players,
    (SELECT count(*) FROM moves) AS moves,
    (SELECT count(*) FROM outbox_events) AS pending_events"' sh "$database" >&2
complete=true
printf '%s\n' "$database"
