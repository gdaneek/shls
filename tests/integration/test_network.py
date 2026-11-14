"""Проверяет разрешённые и запрещённые TCP-соединения между контейнерами по их IP-адресам.
Отличает блокировку firewall от отсутствия маршрута и проверяет рост DROP-счётчика.
Режим FIREWALL=off проверяет ожидаемую связность без проектных правил.
"""

import os
import re
import unittest

from support import compose, run

PG, PG2, REDIS = "10.10.2.3", "10.10.2.5", "10.10.2.4"
API_PUBLIC, API_DATA, API_MSG = "10.10.1.2", "10.10.2.2", "10.10.3.2"
FRONTEND = "10.10.1.3"
BROKERS = {"rabbitmq": "10.10.3.3", "rabbitmq-2": "10.10.3.5", "rabbitmq-3": "10.10.3.6"}


def probe(service, ip, port):
    if service == "frontend":
        command = f'timeout 3 nc -z -v -w 5 {ip} {port} 2>&1; echo "code=$?"'
        output = compose("exec", "-T", service, "sh", "-c", command)
    else:
        command = f"timeout 3 bash -c 'exec 3<>/dev/tcp/{ip}/{port}' 2>&1; echo \"code=$?\""
        output = compose("exec", "-T", service, "bash", "-c", command)
    code = output.rsplit("code=", 1)[-1].strip()
    if code == "0":
        return "OPEN"
    if code == "124":
        return "FILTERED"
    if "unreachable" in output.lower():
        return "UNREACHABLE"
    if "refused" in output.lower():
        return "REFUSED"
    return f"ERROR({output})"


def drop_counter():
    result = run("sudo", "-n", "nft", "list", "table", "bridge", "mini_checkers", check=False)
    if result.returncode:
        return None
    match = re.search(r"counter packets (\d+).*DENY everything else", result.stdout)
    return int(match.group(1)) if match else None


class Network(unittest.TestCase):
    def check_pairs(self, pairs, expected):
        for service, ip, port in pairs:
            with self.subTest(source=service, destination=f"{ip}:{port}"):
                actual = probe(service, ip, port)
                self.assertEqual(actual, expected)
                print(f"PASS {service} -> {ip}:{port} {actual}", flush=True)

    def test_allowed_connections(self):
        pairs = [
            ("frontend", API_PUBLIC, 8000), ("game-api", PG, 5432),
            ("game-api", PG2, 5432), ("postgres", PG2, 5432),
            ("postgres-standby", PG, 5432), ("game-api", REDIS, 6379),
        ]
        pairs += [(source, ip, 5672) for ip in BROKERS.values() for source in ("game-api", "event-worker")]
        pairs += [(source, ip, port) for source in BROKERS for name, ip in BROKERS.items()
                  if source != name for port in (4369, 25672)]
        self.check_pairs(pairs, "OPEN")

    def test_denied_connections(self):
        pairs = [
            ("game-api", FRONTEND, 8080), ("event-worker", API_MSG, 8000),
            ("rabbitmq", API_MSG, 8000), ("redis", PG, 5432),
            ("postgres", REDIS, 6379), ("postgres", API_DATA, 8000),
            ("postgres-standby", REDIS, 6379), ("postgres-standby", API_DATA, 8000),
            ("rabbitmq-2", API_MSG, 8000), ("rabbitmq-3", API_MSG, 8000),
        ]
        pairs += [(source, ip, port) for ip in BROKERS.values()
                  for source in ("game-api", "event-worker") for port in (4369, 25672)]
        firewall = os.environ.get("FIREWALL", "on")
        self.assertIn(firewall, {"on", "off"})
        before = drop_counter() if firewall == "on" else None
        self.check_pairs(pairs, "FILTERED" if firewall == "on" else "OPEN")
        after = drop_counter() if firewall == "on" else None
        if before is not None and after is not None:
            self.assertGreater(after, before, "project firewall drop counter did not increase")
            print(f"PASS firewall DROP counter: {before} -> {after}", flush=True)
        elif firewall == "on":
            print("INFO firewall counter unavailable (requires sudo nft)", flush=True)

    def test_segment_isolation(self):
        pairs = [
            ("event-worker", PG, 5432), ("event-worker", PG2, 5432),
            ("event-worker", REDIS, 6379), ("rabbitmq", PG, 5432),
            ("rabbitmq-2", PG, 5432), ("rabbitmq-3", REDIS, 6379),
            ("redis", BROKERS["rabbitmq"], 5672), ("postgres", API_PUBLIC, 8000),
        ]
        self.check_pairs(pairs, "UNREACHABLE")

if __name__ == "__main__":
    unittest.main(verbosity=2)
