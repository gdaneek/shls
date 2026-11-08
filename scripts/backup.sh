#!/usr/bin/env bash
# Consistent logical PostgreSQL backup outside the container/data volume.
# stdout: completed archive path (for scripts); diagnostics go to stderr.
set -euo pipefail
cd "$(dirname "$0")/.."
umask 077

directory="${BACKUP_DIR:-$PWD/backups}"
days="${BACKUP_RETENTION_DAYS:-7}"
[[ "$days" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid retention days' >&2; exit 2; }
mkdir -p "$directory"
directory="$(realpath "$directory")"
chmod 700 "$directory"
exec 9>"$directory/.backup.lock"
flock -w 60 9

partial="$(mktemp "$directory/mini-checkers-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX.dump.partial")"
trap 'rm -f "$partial"' EXIT
timeout 240 docker compose exec -T postgres sh -c \
  'exec pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc --no-owner --no-acl' >"$partial"
[[ -s "$partial" ]]
# Validate archive structure before publishing it as a successful backup.
docker compose exec -T postgres pg_restore --list <"$partial" >/dev/null
archive="${partial%.partial}"
mv "$partial" "$archive"
# Rotate only our completed archives, and only after a successful new backup.
find "$directory" -maxdepth 1 -type f -name 'mini-checkers-*.dump' \
  -mmin "+$((days * 1440))" -delete
echo "Backup complete: $archive" >&2
printf '%s\n' "$archive"
