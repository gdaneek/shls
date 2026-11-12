"""Read-only startup checks for a running Compose stack."""

import json
import os
import unittest
import urllib.request

from support import SERVICES, compose, container_id, primary, redis, run, sql


class Startup(unittest.TestCase):
    def test_all_services_healthy(self):
        for service in SERVICES:
            with self.subTest(service=service):
                status = run("docker", "inspect", "-f", "{{.State.Health.Status}}", container_id(service)).stdout.strip()
                self.assertEqual(status, "healthy")

    def test_hello_and_frontend(self):
        reply = compose("exec", "-T", "game-api", "curl", "-fsS", "http://127.0.0.1:8000/hello")
        self.assertEqual(json.loads(reply), {"message": "hello-world"})
        for path in ("/", "/api/hello"):
            with self.subTest(path=path):
                compose("exec", "-T", "frontend", "wget", "-q", "-O", "/dev/null", "http://127.0.0.1:8080" + path)

    def test_storage_and_broker(self):
        self.assertIn(primary(), {"postgres", "postgres-standby"})
        self.assertEqual(sql("SELECT 1"), "1")
        self.assertEqual(redis("PING"), "PONG")
        compose("exec", "-T", "rabbitmq", "rabbitmq-diagnostics", "-q", "check_port_connectivity", timeout=60)
        queues = json.loads(compose("exec", "-T", "rabbitmq", "rabbitmqctl", "-q", "list_queues",
                                    "--online", "--timeout", "5", "name", "type", "members", "online", "--formatter", "json"))
        queue = next(item for item in queues if item["name"] == "game.events.quorum")
        self.assertEqual(queue["type"], "quorum")
        self.assertEqual(len(queue["members"]), 3)
        self.assertEqual(len(queue["online"]), 3)

    def test_only_frontend_port_is_published(self):
        for service in SERVICES:
            if service == "frontend":
                continue
            with self.subTest(service=service):
                self.assertEqual(run("docker", "port", container_id(service)).stdout.strip(), "")
        self.assertTrue(compose("port", "frontend", "8080").startswith("127.0.0.1:"))

    @unittest.skipUnless(os.environ.get("CHECKERS_HOST_TEST") == "1", "set CHECKERS_HOST_TEST=1 to check the published host port")
    def test_frontend_from_host(self):
        address = compose("port", "frontend", "8080")
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(f"http://{address}/api/hello", timeout=3) as response:
            self.assertEqual(json.load(response), {"message": "hello-world"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
