#!/usr/bin/env bash
# Assignment 4 check: network segmentation and firewall policy.
# Usage: scripts/test-network.sh            expects the firewall to be applied
#        FIREWALL=off scripts/test-network.sh  expects it to be rolled back
#
# Every probe is a TCP connection attempt from inside a container to an IP
# address (not a name, so a DNS failure cannot pass for a block):
#   OPEN         connection established
#   FILTERED     no answer within the timeout: the firewall dropped the packet
#   UNREACHABLE  no route to the segment: blocked by segmentation
#   REFUSED      the host answered, but nothing listens on the port
set -uo pipefail

cd "$(dirname "$0")/.."

firewall="${FIREWALL:-on}"
failures=0

GAME_API_PUBLIC=10.10.1.2
FRONTEND=10.10.1.3
GAME_API_DATA=10.10.2.2
POSTGRES=10.10.2.3
REDIS=10.10.2.4
GAME_API_MESSAGING=10.10.3.2
RABBITMQ=10.10.3.3

probe() {
  local service="$1" ip="$2" port="$3" output code
  # The Alpine frontend has nc but no bash; other containers use /dev/tcp.
  if [[ "$service" == frontend ]]; then
    output="$(docker compose exec -T frontend sh -c \
      "timeout 3 nc -z -v -w 5 $ip $port 2>&1; echo \"code=\$?\"")"
  else
    output="$(docker compose exec -T "$service" bash -c \
      "timeout 3 bash -c 'exec 3<>/dev/tcp/$ip/$port' 2>&1; echo \"code=\$?\"")"
  fi
  code="${output##*code=}"
  if [[ "$code" == 0 ]]; then
    echo OPEN
  elif [[ "$code" == 124 ]]; then
    echo FILTERED
  elif grep -qi "unreachable" <<<"$output"; then
    echo UNREACHABLE
  elif grep -qi "refused" <<<"$output"; then
    echo REFUSED
  else
    echo "ERROR(${output//$'\n'/ })"
  fi
}

# expect SERVICE IP PORT EXPECTED [EXPECTED_WITHOUT_FIREWALL] DESCRIPTION
expect() {
  local service="$1" ip="$2" port="$3" expected="$4" without="$5" description="$6"
  [[ "$firewall" == off ]] && expected="$without"
  local actual
  actual="$(probe "$service" "$ip" "$port")"
  local line
  line="$(printf '%-13s -> %-10s %-15s %-12s' "$service" "$ip:$port" "$description" "$actual")"
  if [[ "$actual" == "$expected" ]]; then
    echo "PASS  $line"
  else
    echo "FAIL  $line (expected $expected)"
    failures=$((failures + 1))
  fi
}

drop_counter() {
  sudo -n nft list table bridge mini_checkers 2>/dev/null |
    awk '/DENY everything else/ {for (i = 1; i <= NF; i++) if ($i == "packets") print $(i + 1)}'
}

echo "firewall expected: $firewall"
drops_before="$(drop_counter)"

echo "--- explicit ALLOW (same segment, listed in the firewall)"
expect frontend "$GAME_API_PUBLIC" 8000 OPEN OPEN "front>api"
expect game-api "$POSTGRES" 5432 OPEN OPEN "game-api>pg"
expect game-api "$REDIS" 6379 OPEN OPEN "game-api>redis"
expect game-api "$RABBITMQ" 5672 OPEN OPEN "game-api>mq"
expect event-worker "$RABBITMQ" 5672 OPEN OPEN "worker>mq"

echo "--- explicit DENY (same segment, dropped by the firewall)"
expect game-api "$FRONTEND" 8080 FILTERED OPEN "api>front"
expect event-worker "$GAME_API_MESSAGING" 8000 FILTERED OPEN "worker>api"
expect rabbitmq "$GAME_API_MESSAGING" 8000 FILTERED OPEN "mq>api"
expect redis "$POSTGRES" 5432 FILTERED OPEN "redis>pg"
expect postgres "$REDIS" 6379 FILTERED OPEN "pg>redis"
expect postgres "$GAME_API_DATA" 8000 FILTERED OPEN "pg>api"

echo "--- segmentation (different segments, no route)"
expect event-worker "$POSTGRES" 5432 UNREACHABLE UNREACHABLE "worker>pg"
expect event-worker "$REDIS" 6379 UNREACHABLE UNREACHABLE "worker>redis"
expect rabbitmq "$POSTGRES" 5432 UNREACHABLE UNREACHABLE "mq>pg"
expect redis "$RABBITMQ" 5672 UNREACHABLE UNREACHABLE "redis>mq"
expect postgres "$GAME_API_PUBLIC" 8000 UNREACHABLE UNREACHABLE "pg>api-public"

echo "--- perimeter (host)"
for service in game-api postgres redis rabbitmq event-worker; do
  if [[ -z "$(docker port "$(docker compose ps -q "$service")")" ]]; then
    echo "PASS  $service has no ports published on the host"
  else
    echo "FAIL  $service has ports published on the host"
    failures=$((failures + 1))
  fi
done
published="$(docker compose port frontend 8080)"
if [[ "$published" == 127.0.0.1:* ]]; then
  echo "PASS  frontend is published only on loopback ($published)"
else
  echo "FAIL  frontend is published on $published"
  failures=$((failures + 1))
fi

drops_after="$(drop_counter)"
if [[ -n "$drops_after" ]]; then
  echo "INFO  DENY rule counter: ${drops_before:-0} -> $drops_after packets"
elif [[ "$firewall" == on ]]; then
  echo "INFO  DENY rule counter is not readable (needs: sudo nft)"
fi

if ((failures > 0)); then
  echo "Result: ${failures} check(s) failed"
  exit 1
fi
echo "Result: all checks passed"
