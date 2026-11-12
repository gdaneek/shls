#!/usr/bin/env bash
# Print the physical service currently accepting writes. Fail on split brain.
set -euo pipefail
cd "$(dirname "$0")/../.."
primary=""
for node in postgres postgres-standby; do
  if [[ "$(docker compose exec -T "$node" psql -X -U mini_checkers -d mini_checkers \
    -tAc "SELECT NOT pg_is_in_recovery() AND current_setting('transaction_read_only') = 'off'" 2>/dev/null)" == t ]]; then
    [[ -z "$primary" ]] || { echo 'ERROR: both PostgreSQL nodes are writable' >&2; exit 1; }
    primary="$node"
  fi
done
[[ -n "$primary" ]] || { echo 'No writable PostgreSQL node' >&2; exit 1; }
printf '%s\n' "$primary"
