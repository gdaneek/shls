"""Короткие HTTP-замеры и контролируемые перезапуски API с журналом JSONL."""

import argparse
from datetime import datetime, timezone
import json
import math
import os
import statistics
import time
import urllib.error
import urllib.request

from support import ROOT, inspect, interruptible, run


BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8000/api").rstrip("/")
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def request(method, path):
    started = time.monotonic()
    status, body, error = None, None, None
    try:
        req = urllib.request.Request(BASE_URL + path, method=method)
        with HTTP.open(req, timeout=2) as response:
            status = response.status
            body = json.load(response)
    except urllib.error.HTTPError as caught:
        status, error = caught.code, f"HTTP {caught.code}"
    except (OSError, ValueError, urllib.error.URLError) as caught:
        error = str(caught)
    return status, body, time.monotonic() - started, error


def create_fixture():
    status, body, _, error = request("POST", "/games")
    if status != 201 or not isinstance(body, dict) or "game_id" not in body:
        raise RuntimeError(f"Cannot create game: HTTP {status}, {error}")
    return dict(game_id=body["game_id"], status="waiting", version=1,
                current_turn="white", winner=None,
                board=[".b.b.b.b", "b.b.b.b.", ".b.b.b.b", "........",
                       "........", "w.w.w.w.", ".w.w.w.w", "w.w.w.w."])


def probe(expected):
    status, body, elapsed, error = request("GET", "/games/" + expected["game_id"])
    correct = (status == 200 and isinstance(body, dict)
               and all(body.get(key) == value for key, value in expected.items()))
    return dict(ok=correct, status=status, elapsed_s=elapsed,
                error=error if error else (None if correct else "unexpected game state"))


def observe(args, expected, log):
    start = previous = time.monotonic()
    was_ok, failures, uptime, downtime = True, 0, 0.0, 0.0
    samples, latencies = 0, []
    while time.monotonic() - start < args.seconds:
        tick = time.monotonic()
        sample = probe(expected)
        now = time.monotonic()
        # Sample-and-hold: состояние между пробами считается прежним.
        if was_ok:
            uptime += now - previous
        else:
            downtime += now - previous
        if was_ok and not sample["ok"]:
            failures += 1
        was_ok, previous = sample["ok"], now
        samples += 1
        if sample["ok"]:
            latencies.append(sample["elapsed_s"])
        log("probe", offset_s=now - start, **sample)
        time.sleep(max(0, min(1 - (now - tick), args.seconds - (now - start))))
    end = time.monotonic()
    if was_ok:
        uptime += end - previous
    else:
        downtime += end - previous
    success = len(latencies) / samples
    p95 = sorted(latencies)[math.ceil(len(latencies) * 0.95) - 1] if latencies else None
    return dict(
        mode="sli", observation_s=end - start, requests=samples,
        correct_reads=len(latencies), correct_read_ratio=success,
        successful_read_p95_s=p95,
        slo_correct_reads=dict(target=0.99, passed=success >= 0.99),
        slo_p95=dict(target_s=0.5, passed=p95 is not None and p95 <= 0.5),
        observed_read_path_outages=failures, unresolved_outage=not was_ok,
        estimated_uptime_s=uptime, estimated_downtime_s=downtime,
        read_path_mtbf_estimate_s=uptime / failures if failures else None,
        mtbf_note=("Short-window read-path estimate, not weekly service MTBF."
                   if failures else "No observed outages; MTBF is not estimated."),
        timing_note="Outages sampled every ~1s plus request delays; brief outages may be missed.",
    )


def recover(args, expected, log):
    durations = []
    for trial in range(1, args.repeats + 1):
        before = inspect("game-api")
        if (not before["State"]["Running"]
                or before["HostConfig"]["RestartPolicy"]["Name"] not in ("always", "unless-stopped")):
            raise RuntimeError("API must be running with always/unless-stopped restart policy")
        if request("GET", "/health/ready")[0] != 200 or not probe(expected)["ok"]:
            raise RuntimeError("API is not ready before the trial")
        # Работа >10 с сбрасывает backoff Docker между повторениями.
        time.sleep(12)
        started, completed = time.monotonic(), False
        log("fault", trial=trial, service="game-api", signal="SIGTERM",
            previous_restart_count=before["RestartCount"])
        try:
            result = run("docker", "exec", before["Id"], "sh", "-c", "kill -TERM 1",
                         check=False, timeout=10)
            log("signal_result", trial=trial, exit_code=result.returncode)
            streak = 0
            while time.monotonic() - started < 60:
                sample = probe(expected)
                ready = request("GET", "/health/ready")[0] == 200
                state = json.loads(run("docker", "inspect", before["Id"], timeout=5).stdout)[0]
                restarted = (state["RestartCount"] > before["RestartCount"]
                             and state["State"]["Running"])
                elapsed = time.monotonic() - started
                log("recovery_probe", trial=trial, offset_s=elapsed,
                    ready=ready, restarted=restarted, **sample)
                streak = streak + 1 if sample["ok"] and ready and restarted else 0
                if streak >= 3 and elapsed <= 60:
                    durations.append(elapsed)
                    completed = True
                    log("recovered", trial=trial, recovery_s=elapsed,
                        within_30s=elapsed <= 30)
                    print(f"{trial}/{args.repeats}: API recovered in {elapsed:.2f}s", flush=True)
                    break
                time.sleep(0.5)
            if not completed:
                log("recovery_timeout", trial=trial, limit_s=60)
                return dict(mode="mttr", completed_trials=len(durations),
                            recovery_s=durations, mttr_s=None, timed_out_trial=trial,
                            passed=False, note="Unresolved recovery; no aggregate MTTR.")
        finally:
            if not completed:
                # Только исходный API-контейнер; зависимости не затрагиваются.
                run("docker", "start", before["Id"], timeout=30)
                log("cleanup", trial=trial, action="docker start game-api")
    return dict(mode="mttr", completed_trials=len(durations), recovery_s=durations,
                mttr_s=statistics.mean(durations), max_recovery_s=max(durations),
                target_mean_s=30, passed=statistics.mean(durations) <= 30,
                all_trials_within_30s=all(value <= 30 for value in durations),
                scenario="API PID 1 SIGTERM and automatic restart; other services running",
                timing_note="From signal-command start to 3 correct reads and readiness checks after restart; includes Docker/probe overhead.",
                mtbf_note="Injected failures are excluded from operational MTBF.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("sli", "mttr"))
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()
    if args.seconds < 1 or args.repeats < 1:
        parser.error("seconds and repeats must be positive")
    if args.mode == "mttr" and os.environ.get("CHECKERS_DISRUPTIVE") != "1":
        parser.error("set CHECKERS_DISRUPTIVE=1 to restart Game API")
    directory = ROOT / "build/service-metrics"
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = directory / f"{args.mode}-{stamp}.jsonl"
    print(f"Log: {path}", flush=True)
    with path.open("x", encoding="utf-8") as output:
        def log(kind, **fields):
            output.write(json.dumps(dict(kind=kind, utc=datetime.now(timezone.utc).isoformat(),
                                         **fields), ensure_ascii=False) + "\n")
            output.flush()

        log("start", mode=args.mode, base_url=BASE_URL, seconds=args.seconds,
            repeats=args.repeats, request_timeout_s=2,
            boundary="Host HTTP through BASE_URL; HTTP proxy environment ignored")
        try:
            with interruptible():
                expected = create_fixture()
                if not probe(expected)["ok"]:
                    raise RuntimeError("Initial game read failed")
                log("fixture", game_id=expected["game_id"])
                summary = (observe if args.mode == "sli" else recover)(args, expected, log)
                log("summary", **summary)
                print(json.dumps(summary, ensure_ascii=False, indent=2))
                passed = (summary["slo_correct_reads"]["passed"] and summary["slo_p95"]["passed"]
                          if args.mode == "sli" else summary["passed"])
                return 0 if passed else 1
        except (Exception, KeyboardInterrupt) as caught:
            log("aborted", error=str(caught), complete=False)
            print(f"Stopped: {caught}. Partial log: {path}", flush=True)
            return 2


if __name__ == "__main__":
    raise SystemExit(main())
