#!/usr/bin/env bash
set -euo pipefail
if (( $(id -u) == 0 )); then
  chown rabbitmq:rabbitmq /var/lib/rabbitmq
  exec gosu rabbitmq "$0" "$@"
fi
[[ "${RABBITMQ_ERLANG_COOKIE:-}" =~ ^[A-Za-z0-9]{20,64}$ ]] || {
  echo 'Set RABBITMQ_ERLANG_COOKIE to 20-64 alphanumeric characters (openssl rand -hex 32)' >&2
  exit 1
}
cookie=/var/lib/rabbitmq/.erlang.cookie
if [[ -e "$cookie" ]]; then
  [[ "$(cat "$cookie")" == "$RABBITMQ_ERLANG_COOKIE" ]] || {
    echo 'Existing node cookie differs from .env; preserve/reuse it, do not overwrite the volume' >&2
    exit 1
  }
else
  umask 077
  printf '%s' "$RABBITMQ_ERLANG_COOKIE" >"$cookie"
fi
chmod 600 "$cookie"
exec docker-entrypoint.sh "$@"
