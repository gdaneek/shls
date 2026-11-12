"""Общие функции для обращения к Docker Compose, API, базе данных и очереди.
Помогают создавать тестовые партии, ждать восстановления и возвращать остановленные контейнеры.
"""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
SERVICES = (
    "frontend", "game-api", "event-worker", "postgres", "postgres-standby",
    "redis", "rabbitmq", "rabbitmq-2", "rabbitmq-3",
)
BROKERS = ("rabbitmq", "rabbitmq-2", "rabbitmq-3")
DISRUPTIVE = unittest.skipUnless(
    os.environ.get("CHECKERS_DISRUPTIVE") == "1", "set CHECKERS_DISRUPTIVE=1 on a demo stack")


def run(*args, input_text=None, timeout=30, check=True, env=None):
    try:
        result = subprocess.run(
            args, cwd=ROOT, input=input_text, text=True, capture_output=True,
            timeout=timeout, check=False, env=None if env is None else os.environ | env,
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
    result = compose("ps", "-aq", service)
    if not result:
        raise RuntimeError(f"{service} container does not exist")
    return result


def primary():
    return run(str(ROOT / "infra/postgres/postgres-primary.sh")).stdout.strip()


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
    error = None
    while True:
        try:
            value = predicate()
            if value and time.monotonic() <= deadline:
                return value
        except RuntimeError as caught:
            error = caught
        if time.monotonic() >= deadline:
            raise AssertionError(f"condition did not become true within {seconds}s") from error
        time.sleep(interval)


def worker_events(game, event_type=None):
    lines = compose("logs", "--no-log-prefix", "event-worker").splitlines()
    events = [json.loads(line) for line in lines if game in line and line.startswith("{")]
    return [event for event in events if event.get("msg") == "event processed"
            and (event_type is None or event.get("event_type") == event_type)]


def create_game():
    status, _, state = request("POST", "/games")
    if status != 201:
        raise AssertionError(f"creating test game returned HTTP {status}")
    return state


def joined_game():
    white = create_game()
    game = white["game_id"]
    status, _, black = request("POST", f"/games/{game}/join")
    if status != 200:
        raise AssertionError(f"joining test game returned HTTP {status}")
    return game, white["player_token"], black["player_token"]


def seed_position(game, pieces, turn="white"):
    board = ["."] * 64
    assert turn in {"white", "black"}
    for row, col, piece in pieces:
        assert 0 <= row < 8 and 0 <= col < 8 and (row + col) % 2 and piece in "wbWB"
        board[row * 8 + col] = piece
    sql(f"UPDATE games SET board = '{''.join(board)}', current_turn = '{turn}', "
        f"version = version + 1 WHERE id = '{game}'")
    redis("DEL", f"game:{game}")


def outbox_empty(game):
    return sql(f"SELECT count(*) FROM outbox_events WHERE game_id = '{game}'") == "0"


def queue(node="rabbitmq"):
    queues = json.loads(compose("exec", "-T", node, "rabbitmqctl", "-q", "list_queues",
                               "--online", "--timeout", "5", "name", "type", "leader",
                               "members", "online", "messages", "--formatter", "json"))
    for item in queues:
        if item["name"] == "game.events.quorum":
            return item
    raise RuntimeError("game.events.quorum is not online")


def inspect(service):
    return json.loads(run("docker", "inspect", container_id(service)).stdout)[0]


def healthy(service):
    state = inspect(service)["State"]
    return state["Running"] and state["Health"]["Status"] == "healthy"


@contextmanager
def interruptible():
    # Make SIGTERM unwind finally blocks just like Ctrl-C.
    def terminate(signum, frame):
        raise KeyboardInterrupt(f"signal {signum}")
    previous = signal.signal(signal.SIGTERM, terminate)
    try:
        yield
    finally:
        signal.signal(signal.SIGTERM, previous)


@contextmanager
def stopped(*services):
    ids = [container_id(service) for service in services]
    with interruptible():
        try:
            compose("stop", *services, timeout=90)
            yield
        finally:
            # Starting exact IDs cannot revive deliberately failed dependencies.
            run("docker", "start", *ids, timeout=90)


@contextmanager
def crashed(service):
    before = inspect(service)
    policy = before["HostConfig"]["RestartPolicy"]
    restart = policy["Name"]
    if restart == "on-failure" and policy["MaximumRetryCount"]:
        restart += f':{policy["MaximumRetryCount"]}'
    with interruptible():
        try:
            run("docker", "update", "--restart=no", before["Id"])
            compose("kill", "-s", "SIGKILL", service)
            state = inspect(service)["State"]
            assert not state["Running"] and state["ExitCode"] == 137
            yield
        finally:
            try:
                run("docker", "start", before["Id"], timeout=90)
            finally:
                run("docker", "update", f"--restart={restart}", before["Id"])
