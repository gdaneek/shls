#!/usr/bin/env bash
# Runs only as an explicit Compose tools-profile job, never as a DB server.
set -euo pipefail
operation="${1:-}"
target="${2:-}"
case "$target" in
  postgres) directory=/data/postgres ;;
  postgres-standby) directory=/data/postgres-standby/cluster ;;
  *) echo 'Unknown target node' >&2; exit 2 ;;
esac
[[ -f "$directory/PG_VERSION" ]] || { echo 'Target is not an existing cluster' >&2; exit 1; }
if pg_isready -h "$target" -U "$POSTGRES_USER" -d "$POSTGRES_DB" -t 2 >/dev/null 2>&1; then
  echo 'Refusing maintenance on a running target' >&2
  exit 1
fi
case "$operation" in
  fence) touch "$directory/.mini-checkers-fenced" ;;
  archive) tar -C "$directory" -czf - . ;;
  rewind)
    source="${3:-}"
    [[ "$source" == postgres || "$source" == postgres-standby ]] && [[ "$source" != "$target" ]]
    # Keep the node fenced if rewind or recovery configuration fails.
    trap 'touch "$directory/.mini-checkers-fenced"' EXIT
    # The dead postmaster was PID 1 in its container; PID 1 exists in this
    # maintenance container too. After both host-side and TCP stop checks,
    # remove only that stale lock (the physical archive retains a copy).
    rm -f "$directory/postmaster.pid"
    PGPASSWORD="$POSTGRES_PASSWORD" pg_rewind --target-pgdata="$directory" \
      --source-server="host=$source port=5432 user=$POSTGRES_USER dbname=$POSTGRES_DB connect_timeout=5"
    printf "\nprimary_conninfo = 'host=%s port=5432 user=mini_checkers_repl passfile=/tmp/replication.pgpass application_name=%s connect_timeout=5'\n" \
      "$source" "$target" >> "$directory/postgresql.auto.conf"
    touch "$directory/standby.signal"
    rm -f "$directory/.mini-checkers-fenced"
    trap - EXIT
    ;;
  *) echo 'usage: postgres-maintenance.sh fence|archive|rewind TARGET [SOURCE]' >&2; exit 2 ;;
esac
