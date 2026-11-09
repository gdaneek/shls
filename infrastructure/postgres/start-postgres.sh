#!/usr/bin/env bash
# One native PostgreSQL image for both nodes. Existing standby.signal decides
# the role after promotion/rejoin; an existing cluster is never reinitialized.
set -euo pipefail
case "${PGDATA:-}" in
  /var/lib/postgresql/data|/var/lib/postgresql/data/cluster) ;;
  *) echo 'Unexpected PGDATA path' >&2; exit 1 ;;
esac
if (( $(id -u) == 0 )); then
  mkdir -p "$PGDATA"
  chown postgres:postgres "$PGDATA" "$(dirname "$PGDATA")"
  exec gosu postgres "$0" "$@"
fi
if [[ -e "$PGDATA/.mini-checkers-fenced" ]]; then
  echo 'Node is fenced after failover; run scripts/rejoin-postgres.sh, not a normal start' >&2
  exit 1
fi
[[ "${POSTGRES_REPLICATION_PASSWORD:-}" =~ ^[0-9a-fA-F]{64}$ ]] || {
  echo 'POSTGRES_REPLICATION_PASSWORD must be 64 hex characters (openssl rand -hex 32)' >&2
  exit 1
}
umask 077
printf '*:*:*:mini_checkers_repl:%s\n' "$POSTGRES_REPLICATION_PASSWORD" > /tmp/replication.pgpass

if [[ ! -f "$PGDATA/PG_VERSION" && -n "${POSTGRES_STANDBY_HOST:-}" ]]; then
  [[ "$POSTGRES_STANDBY_HOST" == postgres ]]
  [[ -z "$(ls -A "$PGDATA")" ]] || { echo 'Refusing to replace nonempty PGDATA' >&2; exit 1; }
  incoming="${PGDATA}.incoming"
  [[ ! -e "$incoming" ]] || { echo "Inspect unfinished clone: $incoming" >&2; exit 1; }
  # Authenticated bootstrap, using the existing DB administrator only here.
  # Once initialized, the WAL receiver uses mini_checkers_repl, not the admin.
  ready=false
  for ((attempt=0; attempt<30; attempt++)); do
    if PGPASSWORD="$POSTGRES_PASSWORD" PGCONNECT_TIMEOUT=3 psql -X -v ON_ERROR_STOP=1 \
      -h "$POSTGRES_STANDBY_HOST" -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
      -f /usr/local/share/mini-checkers/replication-role.sql >/dev/null; then
      ready=true; break
    fi
    sleep 2
  done
  [[ "$ready" == true ]] || { echo 'Primary did not become ready for replication bootstrap' >&2; exit 1; }
  pg_basebackup -d "host=$POSTGRES_STANDBY_HOST port=5432 user=mini_checkers_repl passfile=/tmp/replication.pgpass application_name=postgres-standby connect_timeout=5" \
    -D "$incoming" -Fp -Xs -R
  rmdir "$PGDATA"  # It was checked empty; never recursively delete a cluster.
  mv "$incoming" "$PGDATA"
fi
if [[ -f "$PGDATA/PG_VERSION" ]] && ! grep -Fq "include_if_exists = '/etc/postgresql/replication.conf'" "$PGDATA/postgresql.conf"; then
  printf "\ninclude_if_exists = '/etc/postgresql/replication.conf'\n" >> "$PGDATA/postgresql.conf"
fi
exec docker-entrypoint.sh "$@"
