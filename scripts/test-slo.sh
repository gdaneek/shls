#!/usr/bin/env bash
# Small acceptance sample, NOT a load test or a monthly availability estimate.
# SLI: correct reads / all reads; p95 of correct read latency (curl time_total);
#      events processed within 15s / committed create events.
# SLO: >=99% correct reads, p95 <=0.5s, 100% timely events (5 samples).
# Measure from inside game-api, without Docker exec startup or host/VPN latency.
set -euo pipefail
cd "$(dirname "$0")/.."
temporary="$(mktemp -d)"
trap 'rm -f "$temporary/reads" "$temporary/logs"; rmdir "$temporary"' EXIT

api() { docker compose exec -T game-api curl -sS --max-time 5 "$@"; }
created="$(api -f -X POST http://127.0.0.1:8000/games)"
game="$(jq -er .game_id <<<"$created")"

# A fixed waiting game has version=1 throughout the read sample. Count bad
# responses and transport failures as failed requests, not as fast successes.
good=0
for ((i=0; i<100; i++)); do
  raw="$(api -w $'\n%{http_code} %{time_total}' "http://127.0.0.1:8000/games/$game")" || true
  read -r status elapsed <<<"${raw##*$'\n'}"
  if [[ "$status" == 200 ]] && jq -e --arg id "$game" \
    '.game_id == $id and .status == "waiting" and .version == 1
     and .current_turn == "white" and .winner == null
     and .board == [".b.b.b.b", "b.b.b.b.", ".b.b.b.b", "........",
                    "........", "w.w.w.w.", ".w.w.w.w", "w.w.w.w."]' \
    >/dev/null 2>&1 <<<"${raw%$'\n'*}"; then
    good=$((good + 1))
    printf '%s\n' "$elapsed" >>"$temporary/reads"
  fi
done
failures=0
if ((good >= 99)); then
  echo "PASS SLI correct reads=$good/100; SLO >=99%"
else
  echo "FAIL SLI correct reads=$good/100; SLO >=99%"
  failures=$((failures + 1))
fi
if ((good > 0)); then
  # Nearest-rank percentile, only over correct responses. Failed requests
  # remain visible in the availability ratio above.
  p95="$(sort -n "$temporary/reads" | awk -v n="$good" \
    'NR == int((95*n+99)/100) {print; exit}')"
  if awk -v p="$p95" 'BEGIN {exit !(p <= 0.5)}'; then
    echo "PASS SLI p95=${p95}s; SLO <=0.5s"
  else
    echo "FAIL SLI p95=${p95}s; SLO <=0.5s"
    failures=$((failures + 1))
  fi
else
  echo 'FAIL no correct responses for p95'
  failures=$((failures + 1))
fi

timely=0
for ((i=0; i<5; i++)); do
  start=$SECONDS
  game="$(api -f -X POST http://127.0.0.1:8000/games | jq -er .game_id)"
  # Fresh UUID identifies this create event; repeated deliveries count once.
  # Include POST/commit/exec time in the bound (conservative end-to-end probe).
  while ((SECONDS - start <= 15)); do
    docker compose logs --no-log-prefix event-worker >"$temporary/logs"
    if ((SECONDS - start <= 15)) && \
      grep -F "$game" "$temporary/logs" | grep -F '"event_type":"game.created"' >/dev/null; then
      timely=$((timely + 1))
      break
    fi
    sleep 1
  done
done
if ((timely == 5)); then
  echo 'PASS SLI events processed within 15s=5/5; SLO 100%'
else
  echo "FAIL SLI events processed within 15s=$timely/5; SLO 100%"
  failures=$((failures + 1))
fi
echo "Result: $failures failed SLO checks"
((failures == 0))
