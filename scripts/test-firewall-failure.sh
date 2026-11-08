#!/usr/bin/env bash
# Local/demo only: temporarily block a REQUIRED API -> PostgreSQL connection.
# No containers are stopped. Existing project/host rules are never modified.
# Requires the normal firewall and stack to be up, jq, and sudo access to nft.
set -euo pipefail
cd "$(dirname "$0")/.."

nft=(sudo -n /usr/sbin/nft)
((EUID == 0)) && nft=(/usr/sbin/nft)
table="mini_checkers_fault_$$"
applied=false
cleanup() {
  if [[ "$applied" == true ]]; then
    if "${nft[@]}" list table bridge "$table" >/dev/null 2>&1; then
      "${nft[@]}" delete table bridge "$table" || {
        echo "ERROR: rollback failed; run: sudo nft delete table bridge $table" >&2
        return 1
      }
      echo "INFO removed temporary firewall table: bridge $table"
    fi
    applied=false
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

api() { docker compose exec -T game-api curl -sS --max-time 8 "$@"; }
code() { api -o /dev/null -w '%{http_code}' "http://127.0.0.1:8000$1"; }
expect_code() {
  local actual
  actual="$(code "$1")"
  if [[ "$actual" != "$2" ]]; then
    echo "FAIL $1: HTTP $actual, expected $2" >&2
    exit 1
  fi
  echo "PASS $1: HTTP $actual"
}
database_works() {
  [[ "$(docker compose exec -T postgres psql -X -v ON_ERROR_STOP=1 \
    -U mini_checkers -d mini_checkers -tAc 'SELECT 1')" == 1 ]]
}

# -s omits counters so live traffic does not affect policy comparisons.
policy_before="$("${nft[@]}" -s list table bridge mini_checkers)"
if "${nft[@]}" list table bridge "$table" >/dev/null 2>&1; then
  echo "Refusing to touch an existing table: bridge $table" >&2
  exit 1
fi
expect_code /health/ready 200
database_works
# Create only our own fixture BEFORE the fault. During the fault we send
# reads, not writes: a DB timeout must not be mistaken for a rolled-back POST.
game="$(api -f -X POST http://127.0.0.1:8000/games | jq -er .game_id)"
expect_code "/games/$game" 200

echo "INFO applying temporary DROP for game-api -> postgres in bridge $table"
# Mark ownership before loading so SIGINT/TERM immediately after nft also
# executes rollback. A per-process name avoids touching another test's table.
applied=true
"${nft[@]}" -f - <<NFT
table bridge $table {
  chain forward {
    type filter hook forward priority -10; policy accept;
    meta ibrname "mc-data" ip saddr 10.10.2.2 ip daddr 10.10.2.3 tcp dport 5432 counter drop comment "test: deny required API to PostgreSQL"
  }
}
NFT
# Priority -10 runs before the normal established-connection ACCEPT, so
# already-open libpq connections are blocked too; no conntrack flush needed.
expect_code /health/live 200
expect_code /health/ready 503
docker compose exec -T redis redis-cli DEL "game:$game" >/dev/null
expect_code "/games/$game" 503
database_works
echo 'PASS PostgreSQL still executes SQL; it was not stopped'
packets="$("${nft[@]}" list table bridge "$table" |
  awk '/counter packets/ {for(i=1;i<=NF;i++) if($i=="packets") print $(i+1)}')"
[[ "$packets" =~ ^[0-9]+$ ]] && ((packets > 0))
echo "PASS temporary DROP counter: $packets packets"

cleanup
deadline=$((SECONDS + 30))
until [[ "$(code /health/ready)" == 200 ]]; do
  ((SECONDS < deadline)) || { echo 'FAIL readiness did not recover' >&2; exit 1; }
  sleep 1
done
expect_code /health/ready 200
expect_code "/games/$game" 200
[[ "$("${nft[@]}" -s list table bridge mini_checkers)" == "$policy_before" ]]
echo 'PASS original firewall policy is unchanged'
echo 'Result: firewall caused the errors; rollback restored service'
