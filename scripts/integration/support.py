"""Small helpers for checks against the running local Compose stack."""

import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SERVICES = (
    "frontend", "game-api", "event-worker", "postgres", "postgres-standby",
    "redis", "rabbitmq", "rabbitmq-2", "rabbitmq-3",
)


def run(*args, input_text=None, timeout=30, check=True):
    try:
        result = subprocess.run(
            args, cwd=ROOT, input=input_text, text=True, capture_output=True,
            timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"command timed out after {timeout}s: {args[0]}") from None
    if check and result.returncode:
        # Do not print command arguments: API requests may contain player tokens.
        raise RuntimeError(f"{args[0]} failed with exit code {result.returncode}: {result.stderr.strip()}")
    return result


def compose(*args, **kwargs):
    return run("docker", "compose", *args, **kwargs).stdout.strip()


def container_id(service):
    result = compose("ps", "-q", service)
    if not result:
        raise RuntimeError(f"{service} is not running")
    return result


def primary():
    return run(str(ROOT / "scripts/postgres-primary.sh")).stdout.strip()


def sql(query, database="mini_checkers", node=None):
    return compose(
        "exec", "-T", node or primary(), "psql", "-X", "-v", "ON_ERROR_STOP=1",
        "-U", "mini_checkers", "-d", database, "-tAc", query,
    )


def redis(*args):
    return compose("exec", "-T", "redis", "redis-cli", *map(str, args))


def request(method, path, token=None, body=None):
    args = ["exec", "-T", "game-api", "curl", "-sS", "--max-time", "8",
            "-X", method, "-D", "-", f"http://127.0.0.1:8000{path}"]
    if token:
        args += ["-H", f"X-Player-Token: {token}"]
    if body is not None:
        args += ["-H", "Content-Type: application/json", "--data", json.dumps(body)]
    raw = compose(*args).replace("\r\n", "\n")
    head, payload = raw.split("\n\n", 1)
    lines = head.splitlines()
    status = int(lines[0].split()[1])
    headers = dict(line.split(":", 1) for line in lines[1:] if ":" in line)
    return status, {key.lower(): value.strip() for key, value in headers.items()}, json.loads(payload)


def wait_for(predicate, seconds=15, interval=0.5):
    deadline = time.monotonic() + seconds
    while True:
        if predicate():
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"condition did not become true within {seconds}s")
        time.sleep(interval)


def worker_events(game):
    lines = compose("logs", "--no-log-prefix", "event-worker").splitlines()
    return [json.loads(line) for line in lines if game in line and line.startswith("{")]
